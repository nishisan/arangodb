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

#include "LogFileImpl.h"

#include "Basics/Exceptions.h"
#include "Basics/Result.h"
#include "Basics/voc-errors.h"
#include "Logger/LogMacros.h"
#include "Replication2/Storage/WAL/LogFileReaderImpl.h"

namespace arangodb::replication2::storage::wal {

LogFileImpl::LogFileImpl(std::string path) : _path(std::move(path)) {
  LOG_DEVEL << "opening log file " << path;
  _file = std::fopen(_path.c_str(), "a+b");
  if (_file == nullptr) {
    THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_INTERNAL,
                                   "failed to open replicated log file");
  }
}

LogFileImpl::~LogFileImpl() {
  if (_file) {
    std::fclose(_file);
  }
}

auto LogFileImpl::append(std::string_view data) -> Result {
  auto n = std::fwrite(data.data(), data.size(), 1, _file);
  if (n != data.size()) {
    return Result(TRI_ERROR_INTERNAL, "failed to write to log file");
  }
  return Result{};
}

void LogFileImpl::sync() {
  ADB_PROD_ASSERT(std::fflush(_file) == 0)
      << "failed to flush: " << strerror(errno);
  ADB_PROD_ASSERT(fdatasync(fileno(_file)) == 0)
      << "failed to flush: " << strerror(errno);
}

auto LogFileImpl::getReader() const -> std::unique_ptr<IFileReader> {
  return std::make_unique<LogFileReaderImpl>(_path);
}

}  // namespace arangodb::replication2::storage::wal
