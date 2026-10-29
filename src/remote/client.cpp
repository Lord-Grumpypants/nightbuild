#include "client.hpp"

#include "build/bazel/remote/execution/v2/remote_execution.grpc.pb.h"

#include <chrono>
#include <stdexcept>
#include <string>

#include <grpcpp/grpcpp.h>

namespace nightbuild::remote {

ServerCapabilities get_capabilities(
    const std::string &endpoint,
    bool insecure)
{
    if (endpoint.empty())
        throw std::invalid_argument("remote execution endpoint is empty");

    const auto credentials = insecure
        ? grpc::InsecureChannelCredentials()
        : grpc::SslCredentials(grpc::SslCredentialsOptions{});

    auto channel = grpc::CreateChannel(endpoint, credentials);
    auto stub =
        build::bazel::remote::execution::v2::Capabilities::NewStub(channel);

    build::bazel::remote::execution::v2::GetCapabilitiesRequest request;
    build::bazel::remote::execution::v2::ServerCapabilities response;
    grpc::ClientContext context;
    context.set_deadline(
        std::chrono::system_clock::now() + std::chrono::seconds(10));

    const grpc::Status status =
        stub->GetCapabilities(&context, request, &response);
    if (!status.ok()) {
        throw std::runtime_error(
            "REAPI GetCapabilities failed (" +
            std::to_string(status.error_code()) + "): " +
            status.error_message());
    }

    ServerCapabilities result;
    result.cache_enabled =
        response.has_cache_capabilities() &&
        response.cache_capabilities().digest_functions_size() > 0;
    result.execution_enabled =
        response.has_execution_capabilities() &&
        response.execution_capabilities().exec_enabled();

    if (response.has_cache_capabilities()) {
        for (const auto function :
             response.cache_capabilities().digest_functions()) {
            result.digest_functions.push_back(
                static_cast<std::int32_t>(function));
        }
    }
    return result;
}

} // namespace nightbuild::remote
