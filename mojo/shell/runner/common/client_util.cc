// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/shell/runner/common/client_util.h"

#include <string>

#include "base/command_line.h"
#include "mojo/edk/embedder/embedder.h"
#include "mojo/shell/runner/common/switches.h"

namespace mojo {
namespace shell {

mojom::ShellClientPtr PassShellClientRequestOnCommandLine(
    base::CommandLine* command_line) {
  std::string token = edk::GenerateRandomToken();
  command_line->AppendSwitchASCII(switches::kPrimordialPipeToken, token);

  mojom::ShellClientPtr client;
  client.Bind(
      mojom::ShellClientPtrInfo(edk::CreateParentMessagePipe(token), 0));
  return client;
}

mojom::ShellClientRequest GetShellClientRequestFromCommandLine() {
  std::string token =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          switches::kPrimordialPipeToken);
  mojom::ShellClientRequest request;
  if (!token.empty())
    request.Bind(edk::CreateChildMessagePipe(token));
  return request;
}

}  // namespace shell
}  // namespace mojo
