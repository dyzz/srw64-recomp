// http_get and http_download for the update check and the HD pack's setup step
// (update_check.hpp, hd_prepare.hpp) through NSURLSession: no interface, only the system's
// HTTP and its certificates.
#import <Foundation/Foundation.h>
#include "update_check.hpp"
#include <fstream>
#include <stdexcept>

// The session's delegate: the response's status, then its bytes as they come, into a
// string (the update check, with its limit) or a file (the HD pack).
@interface SRW64HttpReceiver : NSObject <NSURLSessionDataDelegate> {
@public
    std::string* body;  // one of body and file is set
    std::ofstream* file;
    const std::function<void(uint64_t)>* report;
    uint64_t received;
    std::string error;
    dispatch_semaphore_t done;
}
@end

@implementation SRW64HttpReceiver
- (void)URLSession:(NSURLSession*)session dataTask:(NSURLSessionDataTask*)task didReceiveResponse:(NSURLResponse*)response
                                    completionHandler:(void (^)(NSURLSessionResponseDisposition))completionHandler {
    if ([response isKindOfClass:[NSHTTPURLResponse class]] && ((NSHTTPURLResponse*)response).statusCode != 200) {
        error = "HTTP " + std::to_string(((NSHTTPURLResponse*)response).statusCode);
        completionHandler(NSURLSessionResponseCancel);
        return;
    }
    completionHandler(NSURLSessionResponseAllow);
}
- (void)URLSession:(NSURLSession*)session dataTask:(NSURLSessionDataTask*)task didReceiveData:(NSData*)data {
    if (!error.empty()) return;
    if (body) {
        if (body->size() + data.length > (1u << 20)) {
            error = "response too large";
            [task cancel];
            return;
        }
        body->append(static_cast<const char*>(data.bytes), data.length);
    } else {
        file->write(static_cast<const char*>(data.bytes), std::streamsize(data.length));
        if (!*file) {
            error = "write failed";
            [task cancel];
            return;
        }
    }
    received += data.length;
    if (report && *report) (*report)(received);
}
- (void)URLSession:(NSURLSession*)session task:(NSURLSessionTask*)task didCompleteWithError:(NSError*)failure {
    if (failure && error.empty()) error = failure.localizedDescription.UTF8String ?: "request failed";
    dispatch_semaphore_signal(done);
}
@end

namespace srw64::update {
namespace {
void request(const std::string& url, const std::string& user_agent, int timeout_seconds, SRW64HttpReceiver* receiver) {
    NSURL* address = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
    if (!address) throw std::runtime_error("bad URL");
    NSURLSessionConfiguration* configuration = [NSURLSessionConfiguration ephemeralSessionConfiguration];
    // The request timeout is the longest wait between bytes; the resource timeout bounds
    // the whole transfer, which for the HD pack is a long one.
    configuration.timeoutIntervalForRequest = timeout_seconds;
    configuration.timeoutIntervalForResource = receiver->body ? timeout_seconds : 6 * 60 * 60;
    configuration.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
    configuration.HTTPAdditionalHeaders = @{@"User-Agent": [NSString stringWithUTF8String:user_agent.c_str()]};
    receiver->done = dispatch_semaphore_create(0);
    NSURLSession* session = [NSURLSession sessionWithConfiguration:configuration delegate:receiver delegateQueue:nil];
    [[session dataTaskWithURL:address] resume];
    dispatch_semaphore_wait(receiver->done, DISPATCH_TIME_FOREVER);
    [session finishTasksAndInvalidate];
    if (!receiver->error.empty()) throw std::runtime_error(receiver->error);
}
}
std::string http_get(const std::string& url, const std::string& user_agent, int timeout_seconds) {
    std::string body;
    SRW64HttpReceiver* receiver = [SRW64HttpReceiver new];
    receiver->body = &body;
    request(url, user_agent, timeout_seconds, receiver);
    return body;
}
void http_download(const std::string& url, const std::string& user_agent, const std::filesystem::path& file,
                   const std::function<void(uint64_t)>& received, int timeout_seconds) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + file.string());
    SRW64HttpReceiver* receiver = [SRW64HttpReceiver new];
    receiver->file = &out;
    receiver->report = &received;
    request(url, user_agent, timeout_seconds, receiver);
    out.close();
    if (!out) throw std::runtime_error("write failed");
}
}
