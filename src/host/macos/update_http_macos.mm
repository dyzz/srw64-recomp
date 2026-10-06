// http_get for the update check (update_check.hpp) through NSURLSession: no interface,
// only the system's HTTP and its certificates.
#import <Foundation/Foundation.h>
#include "update_check.hpp"
#include <stdexcept>

namespace srw64::update {
std::string http_get(const std::string& url, int timeout_seconds) {
    NSURL* address = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
    if (!address) throw std::runtime_error("bad URL");
    NSURLSessionConfiguration* configuration = [NSURLSessionConfiguration ephemeralSessionConfiguration];
    configuration.timeoutIntervalForRequest = timeout_seconds;
    configuration.timeoutIntervalForResource = timeout_seconds;
    configuration.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
    NSURLSession* session = [NSURLSession sessionWithConfiguration:configuration];
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    __block std::string body, error;
    NSURLSessionDataTask* task = [session dataTaskWithURL:address
                                        completionHandler:^(NSData* data, NSURLResponse* response, NSError* failure) {
        if (failure) error = failure.localizedDescription.UTF8String ?: "request failed";
        else if ([response isKindOfClass:[NSHTTPURLResponse class]] && ((NSHTTPURLResponse*)response).statusCode != 200)
            error = "HTTP " + std::to_string(((NSHTTPURLResponse*)response).statusCode);
        else if (data.length > (1u << 20)) error = "response too large";
        else body.assign(static_cast<const char*>(data.bytes), data.length);
        dispatch_semaphore_signal(done);
    }];
    [task resume];
    dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
    [session finishTasksAndInvalidate];
    if (!error.empty()) throw std::runtime_error(error);
    return body;
}
}
