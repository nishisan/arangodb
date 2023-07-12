////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2023 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Manuel Pöter
////////////////////////////////////////////////////////////////////////////////

#include "LogFileReaderImpl.h"

#include "Basics/Exceptions.h"

namespace arangodb::replication2::storage::wal {

LogFileReaderImpl::LogFileReaderImpl(std::string const& path) {
  _file = std::fopen(path.c_str(), "rb");
  if (_file == nullptr) {
    THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_INTERNAL,
                                   "failed to open replicated log file");
  }
}

LogFileReaderImpl::~LogFileReaderImpl() {
  if (_file) {
    std::fclose(_file);
  }
}

auto LogFileReaderImpl::read(void* buffer, std::size_t n) -> bool {
  return std::fread(buffer, 1, n, _file) == n;
}

void LogFileReaderImpl::seek(std::uint64_t pos) {
  std::fseek(_file, pos, SEEK_SET);
}

auto LogFileReaderImpl::position() const -> std::uint64_t {
  return std::ftell(_file);
}

}  // namespace arangodb::replication2::storage::wal
