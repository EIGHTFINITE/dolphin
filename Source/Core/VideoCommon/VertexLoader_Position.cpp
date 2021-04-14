// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "VideoCommon/VertexLoader_Position.h"

#include <limits>
#include <type_traits>

#include "Common/CommonTypes.h"
#include "Common/Swap.h"

#include "VideoCommon/DataReader.h"
#include "VideoCommon/VertexLoader.h"
#include "VideoCommon/VertexLoaderManager.h"
#include "VideoCommon/VertexLoaderUtils.h"
#include "VideoCommon/VideoCommon.h"

namespace
{
template <typename T>
constexpr float PosScale(T val, float scale)
{
  return val * scale;
}

template <>
constexpr float PosScale(float val, [[maybe_unused]] float scale)
{
  return val;
}

template <typename T, int N>
void Pos_ReadDirect(VertexLoader* loader)
{
  static_assert(N <= 3, "N > 3 is not sane!");
  const auto scale = loader->m_posScale;
  DataReader dst(g_vertex_manager_write_ptr, nullptr);
  DataReader src(g_video_buffer_read_ptr, nullptr);

  for (int i = 0; i < N; ++i)
  {
    const float value = PosScale(src.Read<T>(), scale);
    if (loader->m_counter < 3)
      VertexLoaderManager::position_cache[loader->m_counter][i] = value;
    dst.Write(value);
  }

  g_vertex_manager_write_ptr = dst.GetPointer();
  g_video_buffer_read_ptr = src.GetPointer();
  LOG_VTX();
}

template <typename I, typename T, int N>
void Pos_ReadIndex(VertexLoader* loader)
{
  static_assert(std::is_unsigned<I>::value, "Only unsigned I is sane!");
  static_assert(N <= 3, "N > 3 is not sane!");

  const auto index = DataRead<I>();
  loader->m_vertexSkip = index == std::numeric_limits<I>::max();
  const auto data =
      reinterpret_cast<const T*>(VertexLoaderManager::cached_arraybases[ARRAY_POSITION] +
                                 (index * g_main_cp_state.array_strides[ARRAY_POSITION]));
  const auto scale = loader->m_posScale;
  DataReader dst(g_vertex_manager_write_ptr, nullptr);

  for (int i = 0; i < N; ++i)
  {
    const float value = PosScale(Common::FromBigEndian(data[i]), scale);
    if (loader->m_counter < 3)
      VertexLoaderManager::position_cache[loader->m_counter][i] = value;
    dst.Write(value);
  }

  g_vertex_manager_write_ptr = dst.GetPointer();
  LOG_VTX();
}

constexpr TPipelineFunction s_table_read_position[4][8][2] = {
    {
        {
            nullptr,
            nullptr,
        },
        {
            nullptr,
            nullptr,
        },
        {
            nullptr,
            nullptr,
        },
        {
            nullptr,
            nullptr,
        },
        {
            nullptr,
            nullptr,
        },
    },
    {
        {
            Pos_ReadDirect<u8, 2>,
            Pos_ReadDirect<u8, 3>,
        },
        {
            Pos_ReadDirect<s8, 2>,
            Pos_ReadDirect<s8, 3>,
        },
        {
            Pos_ReadDirect<u16, 2>,
            Pos_ReadDirect<u16, 3>,
        },
        {
            Pos_ReadDirect<s16, 2>,
            Pos_ReadDirect<s16, 3>,
        },
        {
            Pos_ReadDirect<float, 2>,
            Pos_ReadDirect<float, 3>,
        },
    },
    {
        {
            Pos_ReadIndex<u8, u8, 2>,
            Pos_ReadIndex<u8, u8, 3>,
        },
        {
            Pos_ReadIndex<u8, s8, 2>,
            Pos_ReadIndex<u8, s8, 3>,
        },
        {
            Pos_ReadIndex<u8, u16, 2>,
            Pos_ReadIndex<u8, u16, 3>,
        },
        {
            Pos_ReadIndex<u8, s16, 2>,
            Pos_ReadIndex<u8, s16, 3>,
        },
        {
            Pos_ReadIndex<u8, float, 2>,
            Pos_ReadIndex<u8, float, 3>,
        },
    },
    {
        {
            Pos_ReadIndex<u16, u8, 2>,
            Pos_ReadIndex<u16, u8, 3>,
        },
        {
            Pos_ReadIndex<u16, s8, 2>,
            Pos_ReadIndex<u16, s8, 3>,
        },
        {
            Pos_ReadIndex<u16, u16, 2>,
            Pos_ReadIndex<u16, u16, 3>,
        },
        {
            Pos_ReadIndex<u16, s16, 2>,
            Pos_ReadIndex<u16, s16, 3>,
        },
        {
            Pos_ReadIndex<u16, float, 2>,
            Pos_ReadIndex<u16, float, 3>,
        },
    },
};

constexpr u32 s_table_read_position_vertex_size[4][8][2] = {
    {
        {0, 0},
        {0, 0},
        {0, 0},
        {0, 0},
        {0, 0},
    },
    {
        {2, 3},
        {2, 3},
        {4, 6},
        {4, 6},
        {8, 12},
    },
    {
        {1, 1},
        {1, 1},
        {1, 1},
        {1, 1},
        {1, 1},
    },
    {
        {2, 2},
        {2, 2},
        {2, 2},
        {2, 2},
        {2, 2},
    },
};
}  // Anonymous namespace

u32 VertexLoader_Position::GetSize(VertexComponentFormat type, ComponentFormat format,
                                   CoordComponentCount elements)
{
  return s_table_read_position_vertex_size[u32(type)][u32(format)][u32(elements)];
}

TPipelineFunction VertexLoader_Position::GetFunction(VertexComponentFormat type,
                                                     ComponentFormat format,
                                                     CoordComponentCount elements)
{
  return s_table_read_position[u32(type)][u32(format)][u32(elements)];
}
