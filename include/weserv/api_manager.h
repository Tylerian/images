#pragma once

#include <memory>
#include <string>

#include <weserv/env_interface.h>
#include <weserv/io/source_interface.h>
#include <weserv/io/target_interface.h>
#include <weserv/utils/status.h>

namespace weserv {
namespace api {

/**
 * An API Manager interface.
 */
class ApiManager {
 public:
    virtual ~ApiManager() = default;

    /**
     * Process from and to a custom source/sink.
     * @param query Query string.
     * @param source Source to read from.
     * @param target Target to write to.
     * @return A Status object to represent an error or an OK state.
     */
    virtual utils::Status
    process(const std::string &query,
            std::unique_ptr<io::SourceInterface> source,
            std::unique_ptr<io::TargetInterface> target) = 0;

    /**
     * Process from and to a file.
     * @param query Query string.
     * @param in_file Input file.
     * @param out_file Output file.
     * @return A Status object to represent an error or an OK state.
     */
    virtual utils::Status process_file(const std::string &query,
                                       const std::string &in_file,
                                       const std::string &out_file) = 0;

    /**
     * Process from a file to a memory buffer.
     * @param query Query string.
     * @param in_file Input file.
     * @param out_buf Output buffer.
     * @return A Status object to represent an error or an OK state.
     */
    virtual utils::Status process_file(const std::string &query,
                                       const std::string &in_file,
                                       std::string *out_buf) = 0;

    /**
     * Process from and to a memory buffer.
     * @param query Query string.
     * @param in_buf Input buffer.
     * @param out_buf Output buffer.
     * @return A Status object to represent an error or an OK state.
     */
    virtual utils::Status process_buffer(const std::string &query,
                                         const std::string &in_buf,
                                         std::string *out_buf) = 0;

 protected:
    ApiManager() = default;
};

class ApiManagerFactory {
 public:
    ApiManagerFactory() = default;

    virtual ~ApiManagerFactory() = default;

    /**
     * Create an ApiManager object.
     * @param env A smart pointer to the environment.
     * @return A smart pointer to the ApiManager object.
     */
    std::shared_ptr<ApiManager>
    create_api_manager(std::unique_ptr<ApiEnvInterface> env);
};

}  // namespace api
}  // namespace weserv
