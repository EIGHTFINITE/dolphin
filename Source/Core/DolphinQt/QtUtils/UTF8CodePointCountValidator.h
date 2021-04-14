// Copyright 2020 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <cstddef>

#include <QString>
#include <QValidator>

class UTF8CodePointCountValidator : public QValidator
{
  Q_OBJECT
public:
  explicit UTF8CodePointCountValidator(std::size_t max_count, QObject* parent = nullptr);

  QValidator::State validate(QString& input, int& pos) const override;

private:
  std::size_t m_max_count;
};
