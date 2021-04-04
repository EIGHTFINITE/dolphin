// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

// No Wii socket support while using NetPlay or TAS
#include "Core/IOS/Network/Socket.h"

#include <algorithm>
#include <numeric>

#include <mbedtls/error.h>
#ifndef _WIN32
#include <arpa/inet.h>
#include <unistd.h>
#endif
#ifdef __HAIKU__
#include <sys/select.h>
#endif

#include "Common/FileUtil.h"
#include "Common/IOFile.h"
#include "Core/Config/MainSettings.h"
#include "Core/Core.h"
#include "Core/IOS/Device.h"
#include "Core/IOS/IOS.h"
#include "Core/PowerPC/PowerPC.h"

#ifdef _WIN32
#define ERRORCODE(name) WSA##name
#define EITHER(win32, posix) win32
#else
#define ERRORCODE(name) name
#define EITHER(win32, posix) posix
#define closesocket close
#endif

namespace IOS::HLE
{
char* WiiSockMan::DecodeError(s32 ErrorCode)
{
#ifdef _WIN32
  // NOT THREAD SAFE
  static char Message[1024];

  FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS |
                     FORMAT_MESSAGE_MAX_WIDTH_MASK,
                 nullptr, ErrorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), Message,
                 sizeof(Message), nullptr);

  return Message;
#else
  return strerror(ErrorCode);
#endif
}

// The following functions can return
//  - EAGAIN / EWOULDBLOCK: send(to), recv(from), accept
//  - EINPROGRESS: connect, bind
//  - WSAEWOULDBLOCK: send(to), recv(from), accept, connect
// On Windows is_rw is used to correct the return value for connect
static s32 TranslateErrorCode(s32 native_error, bool is_rw)
{
  switch (native_error)
  {
  case ERRORCODE(EMSGSIZE):
    ERROR_LOG_FMT(IOS_NET, "Find out why this happened, looks like PEEK failure?");
    return -1;  // Should be -SO_EMSGSIZE
  case EITHER(WSAENOTSOCK, EBADF):
    return -SO_EBADF;
  case ERRORCODE(EADDRINUSE):
    return -SO_EADDRINUSE;
  case ERRORCODE(ECONNRESET):
    return -SO_ECONNRESET;
  case ERRORCODE(EISCONN):
    return -SO_EISCONN;
  case ERRORCODE(ENOTCONN):
    return -SO_ENOTCONN;
  case ERRORCODE(EINPROGRESS):
    return -SO_EINPROGRESS;
  case ERRORCODE(EALREADY):
    return -SO_EALREADY;
  case ERRORCODE(EACCES):
    return -SO_EACCES;
  case ERRORCODE(ECONNREFUSED):
    return -SO_ECONNREFUSED;
  case ERRORCODE(ENETUNREACH):
    return -SO_ENETUNREACH;
  case ERRORCODE(EHOSTUNREACH):
    return -SO_EHOSTUNREACH;
  case ENOMEM:  // See man (7) ip
  case ERRORCODE(ENOBUFS):
    return -SO_ENOMEM;
  case ERRORCODE(ENETRESET):
    return -SO_ENETRESET;
  case EITHER(WSAEWOULDBLOCK, EAGAIN):
    return (is_rw) ? (-SO_EAGAIN) : (-SO_EINPROGRESS);
  default:
    return -1;
  }
}

// Don't use string! (see https://github.com/dolphin-emu/dolphin/pull/3143)
s32 WiiSockMan::GetNetErrorCode(s32 ret, std::string_view caller, bool is_rw)
{
#ifdef _WIN32
  s32 error_code = WSAGetLastError();
#else
  s32 error_code = errno;
#endif

  if (ret >= 0)
  {
    WiiSockMan::GetInstance().SetLastNetError(ret);
    return ret;
  }

  ERROR_LOG_FMT(IOS_NET, "{} failed with error {}: {}, ret= {}", caller, error_code,
                DecodeError(error_code), ret);

  const s32 return_value = TranslateErrorCode(error_code, is_rw);
  WiiSockMan::GetInstance().SetLastNetError(return_value);

  return return_value;
}

WiiSocket::~WiiSocket()
{
  if (fd >= 0)
  {
    (void)CloseFd();
  }
}

void WiiSocket::SetFd(s32 s)
{
  if (fd >= 0)
    (void)CloseFd();

  nonBlock = false;
  fd = s;

// Set socket to NON-BLOCK
#ifdef _WIN32
  u_long iMode = 1;
  ioctlsocket(fd, FIONBIO, &iMode);
#else
  int flags;
  if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
    flags = 0;
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

void WiiSocket::SetWiiFd(s32 s)
{
  wii_fd = s;
}

s32 WiiSocket::Shutdown(u32 how)
{
  if (how > 2)
    return -SO_EINVAL;

  // The Wii does nothing and returns 0 for IP_PROTO_UDP
  int so_type;
  socklen_t opt_len = sizeof(so_type);
  if (getsockopt(fd, SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&so_type), &opt_len) != 0 ||
      (so_type != SOCK_STREAM && so_type != SOCK_DGRAM))
    return -SO_EBADF;
  if (so_type == SOCK_DGRAM)
    return SO_SUCCESS;

  // Adjust pending operations
  // Values based on https://dolp.in/pr8758 hwtest
  const s32 ret = WiiSockMan::GetNetErrorCode(shutdown(fd, how), "SO_SHUTDOWN", false);
  const bool shut_read = how == 0 || how == 2;
  const bool shut_write = how == 1 || how == 2;
  for (auto& op : pending_sockops)
  {
    // TODO: Create hwtest for SSL
    if (op.is_ssl)
      continue;

    switch (op.net_type)
    {
    case IOCTL_SO_ACCEPT:
      if (shut_write)
        op.Abort(-SO_EINVAL);
      break;
    case IOCTL_SO_CONNECT:
      if (shut_write && !nonBlock)
        op.Abort(-SO_ENETUNREACH);
      break;
    case IOCTLV_SO_RECVFROM:
      if (shut_read)
        op.Abort(-SO_ENOTCONN);
      break;
    case IOCTLV_SO_SENDTO:
      if (shut_write)
        op.Abort(-SO_ENOTCONN);
      break;
    default:
      break;
    }
  }
  return ret;
}

s32 WiiSocket::CloseFd()
{
  s32 ReturnValue = 0;
  if (fd >= 0)
  {
    s32 ret = closesocket(fd);
    ReturnValue = WiiSockMan::GetNetErrorCode(ret, "CloseFd", false);
  }
  else
  {
    ReturnValue = WiiSockMan::GetNetErrorCode(EITHER(WSAENOTSOCK, EBADF), "CloseFd", false);
  }
  fd = -1;

  for (auto it = pending_sockops.begin(); it != pending_sockops.end();)
  {
    GetIOS()->EnqueueIPCReply(it->request, -SO_ENOTCONN);
    it = pending_sockops.erase(it);
  }
  return ReturnValue;
}

s32 WiiSocket::FCntl(u32 cmd, u32 arg)
{
#ifndef F_GETFL
#define F_GETFL 3
#endif
#ifndef F_SETFL
#define F_SETFL 4
#endif
#define F_NONBLOCK 4
  s32 ret = 0;
  if (cmd == F_GETFL)
  {
    ret = nonBlock ? F_NONBLOCK : 0;
  }
  else if (cmd == F_SETFL)
  {
    nonBlock = (arg & F_NONBLOCK) == F_NONBLOCK;
  }
  else
  {
    ERROR_LOG_FMT(IOS_NET, "SO_FCNTL unknown command");
  }

  INFO_LOG_FMT(IOS_NET, "IOCTL_SO_FCNTL({:08x}, {:08X}, {:08X})", wii_fd, cmd, arg);

  return ret;
}

void WiiSocket::Update(bool read, bool write, bool except)
{
  auto it = pending_sockops.begin();
  while (it != pending_sockops.end())
  {
    s32 ReturnValue = 0;
    bool forceNonBlock = false;
    IPCCommandType ct = it->request.command;
    if (!it->is_ssl && ct == IPC_CMD_IOCTL)
    {
      IOCtlRequest ioctl{it->request.address};
      switch (it->net_type)
      {
      case IOCTL_SO_FCNTL:
      {
        u32 cmd = Memory::Read_U32(ioctl.buffer_in + 4);
        u32 arg = Memory::Read_U32(ioctl.buffer_in + 8);
        ReturnValue = FCntl(cmd, arg);
        break;
      }
      case IOCTL_SO_BIND:
      {
        sockaddr_in local_name;
        WiiSockAddrIn* wii_name = (WiiSockAddrIn*)Memory::GetPointer(ioctl.buffer_in + 8);
        WiiSockMan::Convert(*wii_name, local_name);

        int ret = bind(fd, (sockaddr*)&local_name, sizeof(local_name));
        ReturnValue = WiiSockMan::GetNetErrorCode(ret, "SO_BIND", false);

        INFO_LOG_FMT(IOS_NET, "IOCTL_SO_BIND ({:08X}, {}:{}) = {}", wii_fd,
                     inet_ntoa(local_name.sin_addr), Common::swap16(local_name.sin_port), ret);
        break;
      }
      case IOCTL_SO_CONNECT:
      {
        sockaddr_in local_name;
        WiiSockAddrIn* wii_name = (WiiSockAddrIn*)Memory::GetPointer(ioctl.buffer_in + 8);
        WiiSockMan::Convert(*wii_name, local_name);

        int ret = connect(fd, (sockaddr*)&local_name, sizeof(local_name));
        ReturnValue = WiiSockMan::GetNetErrorCode(ret, "SO_CONNECT", false);

        INFO_LOG_FMT(IOS_NET, "IOCTL_SO_CONNECT ({:08x}, {}:{}) = {}", wii_fd,
                     inet_ntoa(local_name.sin_addr), Common::swap16(local_name.sin_port), ret);
        break;
      }
      case IOCTL_SO_ACCEPT:
      {
        s32 ret;
        if (ioctl.buffer_out_size > 0)
        {
          sockaddr_in local_name;
          WiiSockAddrIn* wii_name = (WiiSockAddrIn*)Memory::GetPointer(ioctl.buffer_out);
          WiiSockMan::Convert(*wii_name, local_name);

          socklen_t addrlen = sizeof(sockaddr_in);
          ret = static_cast<s32>(accept(fd, (sockaddr*)&local_name, &addrlen));

          WiiSockMan::Convert(local_name, *wii_name, addrlen);
        }
        else
        {
          ret = static_cast<s32>(accept(fd, nullptr, nullptr));
        }

        ReturnValue = WiiSockMan::GetInstance().AddSocket(ret, true);

        ioctl.Log("IOCTL_SO_ACCEPT", Common::Log::IOS_NET);
        break;
      }
      default:
        break;
      }

      // Fix blocking error codes
      if (!nonBlock && it->net_type == IOCTL_SO_CONNECT)
      {
        switch (ReturnValue)
        {
        case -SO_EAGAIN:
        case -SO_EALREADY:
        case -SO_EINPROGRESS:
          if (std::chrono::steady_clock::now() > GetTimeout())
          {
            ReturnValue = -SO_ENETUNREACH;
            ResetTimeout();
          }
          break;
        case -SO_EISCONN:
          ReturnValue = SO_SUCCESS;
          [[fallthrough]];
        default:
          ResetTimeout();
        }
      }
    }
    else if (ct == IPC_CMD_IOCTLV)
    {
      IOCtlVRequest ioctlv{it->request.address};
      u32 BufferIn = 0, BufferIn2 = 0;
      u32 BufferInSize = 0, BufferInSize2 = 0;
      u32 BufferOut = 0, BufferOut2 = 0;
      u32 BufferOutSize = 0, BufferOutSize2 = 0;

      if (!ioctlv.in_vectors.empty())
      {
        BufferIn = ioctlv.in_vectors.at(0).address;
        BufferInSize = ioctlv.in_vectors.at(0).size;
      }

      if (!ioctlv.io_vectors.empty())
      {
        BufferOut = ioctlv.io_vectors.at(0).address;
        BufferOutSize = ioctlv.io_vectors.at(0).size;
      }

      if (ioctlv.io_vectors.size() > 1)
      {
        BufferOut2 = ioctlv.io_vectors.at(1).address;
        BufferOutSize2 = ioctlv.io_vectors.at(1).size;
      }

      if (ioctlv.in_vectors.size() > 1)
      {
        BufferIn2 = ioctlv.in_vectors.at(1).address;
        BufferInSize2 = ioctlv.in_vectors.at(1).size;
      }

      if (it->is_ssl)
      {
        int sslID = Memory::Read_U32(BufferOut) - 1;
        if (IsSSLIDValid(sslID))
        {
          switch (it->ssl_type)
          {
          case IOCTLV_NET_SSL_DOHANDSHAKE:
          {
            mbedtls_ssl_context* ctx = &NetSSLDevice::_SSL[sslID].ctx;
            const int ret = mbedtls_ssl_handshake(ctx);
            if (ret != 0)
            {
              char error_buffer[256] = "";
              mbedtls_strerror(ret, error_buffer, sizeof(error_buffer));
              ERROR_LOG_FMT(IOS_SSL, "IOCTLV_NET_SSL_DOHANDSHAKE: {}", error_buffer);
            }
            switch (ret)
            {
            case 0:
              WriteReturnValue(SSL_OK, BufferIn);
              break;
            case MBEDTLS_ERR_SSL_WANT_READ:
              WriteReturnValue(SSL_ERR_RAGAIN, BufferIn);
              if (!nonBlock)
                ReturnValue = SSL_ERR_RAGAIN;
              break;
            case MBEDTLS_ERR_SSL_WANT_WRITE:
              WriteReturnValue(SSL_ERR_WAGAIN, BufferIn);
              if (!nonBlock)
                ReturnValue = SSL_ERR_WAGAIN;
              break;
            case MBEDTLS_ERR_X509_CERT_VERIFY_FAILED:
            {
              char error_buffer[256] = "";
              int res = mbedtls_ssl_get_verify_result(ctx);
              mbedtls_x509_crt_verify_info(error_buffer, sizeof(error_buffer), "", res);
              ERROR_LOG_FMT(IOS_SSL, "MBEDTLS_ERR_X509_CERT_VERIFY_FAILED (verify_result = {}): {}",
                            res, error_buffer);

              if (res & MBEDTLS_X509_BADCERT_CN_MISMATCH)
                res = SSL_ERR_VCOMMONNAME;
              else if (res & MBEDTLS_X509_BADCERT_NOT_TRUSTED)
                res = SSL_ERR_VROOTCA;
              else if (res & MBEDTLS_X509_BADCERT_REVOKED)
                res = SSL_ERR_VCHAIN;
              else if (res & MBEDTLS_X509_BADCERT_EXPIRED || res & MBEDTLS_X509_BADCERT_FUTURE)
                res = SSL_ERR_VDATE;
              else
                res = SSL_ERR_FAILED;

              WriteReturnValue(res, BufferIn);
              if (!nonBlock)
                ReturnValue = res;
              break;
            }
            default:
              WriteReturnValue(SSL_ERR_FAILED, BufferIn);
              break;
            }

            // mbedtls_ssl_get_peer_cert(ctx) seems not to work if handshake failed
            // Below is an alternative to dump the peer certificate
            if (Config::Get(Config::MAIN_NETWORK_SSL_DUMP_PEER_CERT) &&
                ctx->session_negotiate != nullptr)
            {
              const mbedtls_x509_crt* cert = ctx->session_negotiate->peer_cert;
              if (cert != nullptr)
              {
                std::string filename = File::GetUserPath(D_DUMPSSL_IDX) +
                                       ((ctx->hostname != nullptr) ? ctx->hostname : "") +
                                       "_peercert.der";
                File::IOFile(filename, "wb").WriteBytes(cert->raw.p, cert->raw.len);
              }
            }

            INFO_LOG_FMT(IOS_SSL,
                         "IOCTLV_NET_SSL_DOHANDSHAKE = ({}) "
                         "BufferIn: ({:08x}, {}), BufferIn2: ({:08x}, {}), "
                         "BufferOut: ({:08x}, {}), BufferOut2: ({:08x}, {})",
                         ret, BufferIn, BufferInSize, BufferIn2, BufferInSize2, BufferOut,
                         BufferOutSize, BufferOut2, BufferOutSize2);
            break;
          }
          case IOCTLV_NET_SSL_WRITE:
          {
            WII_SSL* ssl = &NetSSLDevice::_SSL[sslID];
            const int ret =
                mbedtls_ssl_write(&ssl->ctx, Memory::GetPointer(BufferOut2), BufferOutSize2);

            if (ret >= 0)
            {
              PowerPC::debug_interface.NetworkLogger()->LogSSLWrite(Memory::GetPointer(BufferOut2),
                                                                    ret, ssl->hostfd);
              // Return bytes written or SSL_ERR_ZERO if none
              WriteReturnValue((ret == 0) ? SSL_ERR_ZERO : ret, BufferIn);
            }
            else
            {
              switch (ret)
              {
              case MBEDTLS_ERR_SSL_WANT_READ:
                WriteReturnValue(SSL_ERR_RAGAIN, BufferIn);
                if (!nonBlock)
                  ReturnValue = SSL_ERR_RAGAIN;
                break;
              case MBEDTLS_ERR_SSL_WANT_WRITE:
                WriteReturnValue(SSL_ERR_WAGAIN, BufferIn);
                if (!nonBlock)
                  ReturnValue = SSL_ERR_WAGAIN;
                break;
              default:
                WriteReturnValue(SSL_ERR_FAILED, BufferIn);
                break;
              }
            }
            break;
          }
          case IOCTLV_NET_SSL_READ:
          {
            WII_SSL* ssl = &NetSSLDevice::_SSL[sslID];
            const int ret =
                mbedtls_ssl_read(&ssl->ctx, Memory::GetPointer(BufferIn2), BufferInSize2);

            if (ret >= 0)
            {
              PowerPC::debug_interface.NetworkLogger()->LogSSLRead(Memory::GetPointer(BufferIn2),
                                                                   ret, ssl->hostfd);
              // Return bytes read or SSL_ERR_ZERO if none
              WriteReturnValue((ret == 0) ? SSL_ERR_ZERO : ret, BufferIn);
            }
            else
            {
              switch (ret)
              {
              case MBEDTLS_ERR_SSL_WANT_READ:
                WriteReturnValue(SSL_ERR_RAGAIN, BufferIn);
                if (!nonBlock)
                  ReturnValue = SSL_ERR_RAGAIN;
                break;
              case MBEDTLS_ERR_SSL_WANT_WRITE:
                WriteReturnValue(SSL_ERR_WAGAIN, BufferIn);
                if (!nonBlock)
                  ReturnValue = SSL_ERR_WAGAIN;
                break;
              default:
                WriteReturnValue(SSL_ERR_FAILED, BufferIn);
                break;
              }
            }
            break;
          }
          default:
            break;
          }
        }
        else
        {
          WriteReturnValue(SSL_ERR_ID, BufferIn);
        }
      }
      else
      {
        switch (it->net_type)
        {
        case IOCTLV_SO_SENDTO:
        {
          u32 flags = Memory::Read_U32(BufferIn2 + 0x04);
          u32 has_destaddr = Memory::Read_U32(BufferIn2 + 0x08);

          // Not a string, Windows requires a const char* for sendto
          const char* data = (const char*)Memory::GetPointer(BufferIn);

          // Act as non blocking when SO_MSG_NONBLOCK is specified
          forceNonBlock = ((flags & SO_MSG_NONBLOCK) == SO_MSG_NONBLOCK);
          // send/sendto only handles MSG_OOB
          flags &= SO_MSG_OOB;

          sockaddr_in local_name = {0};
          if (has_destaddr)
          {
            WiiSockAddrIn* wii_name = (WiiSockAddrIn*)Memory::GetPointer(BufferIn2 + 0x0C);
            WiiSockMan::Convert(*wii_name, local_name);
          }

          auto* to = has_destaddr ? reinterpret_cast<sockaddr*>(&local_name) : nullptr;
          socklen_t tolen = has_destaddr ? sizeof(sockaddr) : 0;
          const int ret = sendto(fd, data, BufferInSize, flags, to, tolen);
          ReturnValue = WiiSockMan::GetNetErrorCode(ret, "SO_SENDTO", true);
          if (ret > 0)
            PowerPC::debug_interface.NetworkLogger()->LogWrite(data, ret, fd, to);

          INFO_LOG_FMT(IOS_NET,
                       "{} = {} Socket: {:08x}, BufferIn: ({:08x}, {}), BufferIn2: ({:08x}, {}), "
                       "{}.{}.{}.{}",
                       has_destaddr ? "IOCTLV_SO_SENDTO " : "IOCTLV_SO_SEND ", ReturnValue, wii_fd,
                       BufferIn, BufferInSize, BufferIn2, BufferInSize2,
                       local_name.sin_addr.s_addr & 0xFF, (local_name.sin_addr.s_addr >> 8) & 0xFF,
                       (local_name.sin_addr.s_addr >> 16) & 0xFF,
                       (local_name.sin_addr.s_addr >> 24) & 0xFF);
          break;
        }
        case IOCTLV_SO_RECVFROM:
        {
          u32 flags = Memory::Read_U32(BufferIn + 0x04);
          // Not a string, Windows requires a char* for recvfrom
          char* data = (char*)Memory::GetPointer(BufferOut);
          int data_len = BufferOutSize;

          sockaddr_in local_name;
          memset(&local_name, 0, sizeof(sockaddr_in));

          if (BufferOutSize2 != 0)
          {
            WiiSockAddrIn* wii_name = (WiiSockAddrIn*)Memory::GetPointer(BufferOut2);
            WiiSockMan::Convert(*wii_name, local_name);
          }

          // Act as non blocking when SO_MSG_NONBLOCK is specified
          forceNonBlock = ((flags & SO_MSG_NONBLOCK) == SO_MSG_NONBLOCK);

          // recv/recvfrom only handles PEEK/OOB
          flags &= SO_MSG_PEEK | SO_MSG_OOB;
#ifdef _WIN32
          if (flags & SO_MSG_PEEK)
          {
            unsigned long totallen = 0;
            ioctlsocket(fd, FIONREAD, &totallen);
            ReturnValue = totallen;
            break;
          }
#endif
          socklen_t addrlen = sizeof(sockaddr_in);
          auto* from = BufferOutSize2 ? reinterpret_cast<sockaddr*>(&local_name) : nullptr;
          socklen_t* fromlen = BufferOutSize2 ? &addrlen : nullptr;
          const int ret = recvfrom(fd, data, data_len, flags, from, fromlen);
          ReturnValue =
              WiiSockMan::GetNetErrorCode(ret, BufferOutSize2 ? "SO_RECVFROM" : "SO_RECV", true);
          if (ret > 0)
            PowerPC::debug_interface.NetworkLogger()->LogRead(data, ret, fd, from);

          INFO_LOG_FMT(IOS_NET,
                       "{}({}, {}) Socket: {:08X}, Flags: {:08X}, "
                       "BufferIn: ({:08x}, {}), BufferIn2: ({:08x}, {}), "
                       "BufferOut: ({:08x}, {}), BufferOut2: ({:08x}, {})",
                       BufferOutSize2 ? "IOCTLV_SO_RECVFROM " : "IOCTLV_SO_RECV ", ReturnValue,
                       fmt::ptr(data), wii_fd, flags, BufferIn, BufferInSize, BufferIn2,
                       BufferInSize2, BufferOut, BufferOutSize, BufferOut2, BufferOutSize2);

          if (BufferOutSize2 != 0)
          {
            WiiSockAddrIn* wii_name = (WiiSockAddrIn*)Memory::GetPointer(BufferOut2);
            WiiSockMan::Convert(local_name, *wii_name, addrlen);
          }
          break;
        }
        default:
          break;
        }
      }
    }

    if (it->is_aborted)
    {
      it = pending_sockops.erase(it);
      continue;
    }

    if (nonBlock || forceNonBlock ||
        (!it->is_ssl && ReturnValue != -SO_EAGAIN && ReturnValue != -SO_EINPROGRESS &&
         ReturnValue != -SO_EALREADY) ||
        (it->is_ssl && ReturnValue != SSL_ERR_WAGAIN && ReturnValue != SSL_ERR_RAGAIN))
    {
      DEBUG_LOG_FMT(
          IOS_NET, "IOCTL(V) Sock: {:08x} ioctl/v: {} returned: {} nonBlock: {} forceNonBlock: {}",
          wii_fd, it->is_ssl ? static_cast<int>(it->ssl_type) : static_cast<int>(it->net_type),
          ReturnValue, nonBlock, forceNonBlock);

      // TODO: remove the dependency on a running IOS instance.
      GetIOS()->EnqueueIPCReply(it->request, ReturnValue);
      it = pending_sockops.erase(it);
    }
    else
    {
      ++it;
    }
  }
}

const WiiSocket::Timeout& WiiSocket::GetTimeout()
{
  if (!timeout.has_value())
  {
    timeout = std::chrono::steady_clock::now() +
              std::chrono::seconds(Config::Get(Config::MAIN_NETWORK_TIMEOUT));
  }
  return *timeout;
}

void WiiSocket::ResetTimeout()
{
  timeout.reset();
}

void WiiSocket::DoSock(Request request, NET_IOCTL type)
{
  sockop so = {request, false};
  so.net_type = type;
  pending_sockops.push_back(so);
}

void WiiSocket::DoSock(Request request, SSL_IOCTL type)
{
  sockop so = {request, true};
  so.ssl_type = type;
  pending_sockops.push_back(so);
}

s32 WiiSockMan::AddSocket(s32 fd, bool is_rw)
{
  const char* caller = is_rw ? "SO_ACCEPT" : "NewSocket";

  if (fd < 0)
    return GetNetErrorCode(fd, caller, is_rw);

  s32 wii_fd;
  for (wii_fd = 0; wii_fd < WII_SOCKET_FD_MAX; ++wii_fd)
  {
    // Find an available socket fd
    if (WiiSockets.count(wii_fd) == 0)
      break;
  }

  if (wii_fd == WII_SOCKET_FD_MAX)
  {
    // Close host socket
    closesocket(fd);
    wii_fd = -SO_EMFILE;
    ERROR_LOG_FMT(IOS_NET, "{} failed: Too many open sockets, ret={}", caller, wii_fd);
  }
  else
  {
    WiiSocket& sock = WiiSockets[wii_fd];
    sock.SetFd(fd);
    sock.SetWiiFd(wii_fd);
    PowerPC::debug_interface.NetworkLogger()->OnNewSocket(fd);

#ifdef __APPLE__
    int opt_no_sigpipe = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &opt_no_sigpipe, sizeof(opt_no_sigpipe)) < 0)
      ERROR_LOG_FMT(IOS_NET, "Failed to set SO_NOSIGPIPE on socket");
#endif
  }

  SetLastNetError(wii_fd);
  return wii_fd;
}

bool WiiSockMan::IsSocketBlocking(s32 wii_fd) const
{
  const auto it = WiiSockets.find(wii_fd);
  return it != WiiSockets.end() && !it->second.nonBlock;
}

s32 WiiSockMan::NewSocket(s32 af, s32 type, s32 protocol)
{
  if (af != 2 && af != 23)  // AF_INET && AF_INET6
    return -SO_EAFNOSUPPORT;
  if (protocol != 0)  // IPPROTO_IP
    return -SO_EPROTONOSUPPORT;
  if (type != 1 && type != 2)  // SOCK_STREAM && SOCK_DGRAM
    return -SO_EPROTOTYPE;
  s32 fd = static_cast<s32>(socket(af, type, protocol));
  return AddSocket(fd, false);
}

s32 WiiSockMan::GetHostSocket(s32 wii_fd) const
{
  if (WiiSockets.count(wii_fd) > 0)
    return WiiSockets.at(wii_fd).fd;
  return -EBADF;
}

s32 WiiSockMan::ShutdownSocket(s32 wii_fd, u32 how)
{
  auto socket_entry = WiiSockets.find(wii_fd);
  if (socket_entry != WiiSockets.end())
    return socket_entry->second.Shutdown(how);
  return -SO_EBADF;
}

s32 WiiSockMan::DeleteSocket(s32 wii_fd)
{
  s32 ReturnValue = -SO_EBADF;
  auto socket_entry = WiiSockets.find(wii_fd);
  if (socket_entry != WiiSockets.end())
  {
    ReturnValue = socket_entry->second.CloseFd();
    WiiSockets.erase(socket_entry);
  }
  return ReturnValue;
}

void WiiSockMan::Update()
{
  s32 nfds = 0;
  fd_set read_fds, write_fds, except_fds;
  struct timeval t = {0, 0};
  FD_ZERO(&read_fds);
  FD_ZERO(&write_fds);
  FD_ZERO(&except_fds);

  auto socket_iter = WiiSockets.begin();
  auto end_socks = WiiSockets.end();

  while (socket_iter != end_socks)
  {
    const WiiSocket& sock = socket_iter->second;
    if (sock.IsValid())
    {
      FD_SET(sock.fd, &read_fds);
      FD_SET(sock.fd, &write_fds);
      FD_SET(sock.fd, &except_fds);
      nfds = std::max(nfds, sock.fd + 1);
      ++socket_iter;
    }
    else
    {
      // Good time to clean up invalid sockets.
      socket_iter = WiiSockets.erase(socket_iter);
    }
  }

  const s32 ret = select(nfds, &read_fds, &write_fds, &except_fds, &t);

  if (ret >= 0)
  {
    for (auto& pair : WiiSockets)
    {
      WiiSocket& sock = pair.second;
      sock.Update(FD_ISSET(sock.fd, &read_fds) != 0, FD_ISSET(sock.fd, &write_fds) != 0,
                  FD_ISSET(sock.fd, &except_fds) != 0);
    }
  }
  else
  {
    for (auto& elem : WiiSockets)
    {
      elem.second.Update(false, false, false);
    }
  }
  UpdatePollCommands();
}

void WiiSockMan::UpdatePollCommands()
{
  static constexpr int error_event = (POLLHUP | POLLERR);

  if (pending_polls.empty())
    return;

  const auto now = std::chrono::high_resolution_clock::now();
  const auto elapsed_d = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time);
  const auto elapsed = elapsed_d.count();
  last_time = now;

  for (PollCommand& pcmd : pending_polls)
  {
    // Don't touch negative timeouts
    if (pcmd.timeout > 0)
      pcmd.timeout = std::max<s64>(0, pcmd.timeout - elapsed);
  }

  pending_polls.erase(
      std::remove_if(
          pending_polls.begin(), pending_polls.end(),
          [this](PollCommand& pcmd) {
            const auto request = Request(pcmd.request_addr);
            auto& pfds = pcmd.wii_fds;
            int ret = 0;

            // Happens only on savestate load
            if (pfds[0].revents & error_event)
            {
              ret = static_cast<int>(pfds.size());
            }
            else
            {
              // Make the behavior of poll consistent across platforms by not passing:
              //  - Set with invalid fds, revents is set to 0 (Linux) or POLLNVAL (Windows)
              //  - Set without a valid socket, raises an error on Windows
              std::vector<int> original_order(pfds.size());
              std::iota(original_order.begin(), original_order.end(), 0);
              // Select indices with valid fds
              auto mid = std::partition(original_order.begin(), original_order.end(), [&](auto i) {
                return GetHostSocket(Memory::Read_U32(pcmd.buffer_out + 0xc * i)) >= 0;
              });
              const auto n_valid = std::distance(original_order.begin(), mid);

              // Move all the valid pollfds to the front of the vector
              for (auto i = 0; i < n_valid; ++i)
                std::swap(pfds[i], pfds[original_order[i]]);

              if (n_valid > 0)
                ret = poll(pfds.data(), n_valid, 0);
              if (ret < 0)
                ret = GetNetErrorCode(ret, "UpdatePollCommands", false);

              // Move everything back to where they were
              for (auto i = 0; i < n_valid; ++i)
                std::swap(pfds[i], pfds[original_order[i]]);
            }

            if (ret == 0 && pcmd.timeout)
              return false;

            // Translate native to Wii events,
            for (u32 i = 0; i < pfds.size(); ++i)
            {
              const int revents = ConvertEvents(pfds[i].revents, ConvertDirection::NativeToWii);

              // No need to change fd or events as they are input only.
              // Memory::Write_U32(ufds[i].fd, request.buffer_out + 0xc*i); //fd
              // Memory::Write_U32(events, request.buffer_out + 0xc*i + 4); //events
              Memory::Write_U32(revents, pcmd.buffer_out + 0xc * i + 8);  // revents
              DEBUG_LOG_FMT(IOS_NET,
                            "IOCTL_SO_POLL socket {} wevents {:08X} events {:08X} revents {:08X}",
                            i, revents, pfds[i].events, pfds[i].revents);
            }
            GetIOS()->EnqueueIPCReply(request, ret);
            return true;
          }),
      pending_polls.end());
}

void WiiSockMan::Convert(WiiSockAddrIn const& from, sockaddr_in& to)
{
  to.sin_addr.s_addr = from.addr.addr;
  to.sin_family = from.family;
  to.sin_port = from.port;
}

s32 WiiSockMan::ConvertEvents(s32 events, ConvertDirection dir)
{
  constexpr struct
  {
    int native;
    int wii;
  } mapping[] = {
      {POLLRDNORM, 0x0001}, {POLLRDBAND, 0x0002}, {POLLPRI, 0x0004}, {POLLWRNORM, 0x0008},
      {POLLWRBAND, 0x0010}, {POLLERR, 0x0020},    {POLLHUP, 0x0040}, {POLLNVAL, 0x0080},
  };

  s32 converted_events = 0;
  s32 unhandled_events = 0;

  if (dir == ConvertDirection::NativeToWii)
  {
    for (const auto& map : mapping)
    {
      if (events & map.native)
        converted_events |= map.wii;
    }
  }
  else
  {
    unhandled_events = events;
    for (const auto& map : mapping)
    {
      if (events & map.wii)
        converted_events |= map.native;
      unhandled_events &= ~map.wii;
    }
  }
  if (unhandled_events)
    ERROR_LOG_FMT(IOS_NET, "SO_POLL: unhandled Wii event types: {:04x}", unhandled_events);
  return converted_events;
}

void WiiSockMan::Convert(sockaddr_in const& from, WiiSockAddrIn& to, s32 addrlen)
{
  to.addr.addr = from.sin_addr.s_addr;
  to.family = from.sin_family & 0xFF;
  to.port = from.sin_port;
  if (addrlen < 0 || addrlen > static_cast<s32>(sizeof(WiiSockAddrIn)))
    to.len = sizeof(WiiSockAddrIn);
  else
    to.len = addrlen;
}

void WiiSockMan::DoState(PointerWrap& p)
{
  bool saving =
      p.mode == PointerWrap::Mode::MODE_WRITE || p.mode == PointerWrap::Mode::MODE_MEASURE;
  auto size = pending_polls.size();
  p.Do(size);
  if (!saving)
    pending_polls.resize(size);
  for (auto& pcmd : pending_polls)
  {
    p.Do(pcmd.request_addr);
    p.Do(pcmd.buffer_out);
    p.Do(pcmd.wii_fds);
  }

  if (saving)
    return;
  for (auto& pcmd : pending_polls)
  {
    for (auto& wfd : pcmd.wii_fds)
      wfd.revents = (POLLHUP | POLLERR);
  }
}

void WiiSockMan::AddPollCommand(const PollCommand& cmd)
{
  pending_polls.push_back(cmd);
}

void WiiSockMan::UpdateWantDeterminism(bool want)
{
  // If we switched into movie recording, kill existing sockets.
  if (want)
    Clean();
}

void WiiSocket::sockop::Abort(s32 value)
{
  is_aborted = true;
  GetIOS()->EnqueueIPCReply(request, value);
}
#undef ERRORCODE
#undef EITHER
}  // namespace IOS::HLE
