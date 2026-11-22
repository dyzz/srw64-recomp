#include "hd_pack.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#ifdef _WIN32
// The reader is built everywhere (CMakeLists.txt) though only the Switch mounts a pack:
// the POSIX calls under their Windows names, the file in binary mode, 64-bit offsets.
#include <BaseTsd.h>
#include <io.h>
typedef SSIZE_T ssize_t;
#define SRW64_PACK_OPEN_FLAGS (O_RDONLY | O_BINARY)
#define SRW64_PACK_SEEK _lseeki64
#else
#include <unistd.h>
#define SRW64_PACK_OPEN_FLAGS O_RDONLY
#define SRW64_PACK_SEEK ::lseek
#endif

namespace srw64::hd_pack {
uint64_t fnv1a(std::string_view text) {
    uint64_t value = 0xCBF29CE484222325ull;
    for (const unsigned char byte : text) value = (value ^ byte) * 0x100000001B3ull;
    return value;
}

std::string normal(std::string_view path) {
    std::vector<std::string_view> parts;
    for (size_t at = 0; at <= path.size();) {
        const size_t slash = std::min(path.find('/', at), path.size());
        const auto part = path.substr(at, slash - at);
        if (part == "..") { if (!parts.empty()) parts.pop_back(); }
        else if (!part.empty() && part != ".") parts.push_back(part);
        at = slash + 1;
    }
    std::string out;
    for (const auto part : parts) { if (!out.empty()) out += '/'; out += part; }
    return out;
}

#ifdef __SWITCH__
void close_offset_file(void* file);   // below
#endif
Pack::~Pack() {
    if (file_ >= 0) ::close(file_);
#ifdef __SWITCH__
    if (offset_file_) close_offset_file(offset_file_);
#endif
}

bool Pack::open(const std::string& file) {
    const int handle = ::open(file.c_str(), SRW64_PACK_OPEN_FLAGS);
    if (handle < 0) return false;
    const auto all = [&](void* out, size_t size) { return ::read(handle, out, size) == ssize_t(size); };
    uint32_t head[3] = {}, names_size = 0;
    std::vector<Entry> entries;
    std::string names;
    bool good = all(head, sizeof head) && head[0] == 0x4B50574D && (head[1] == 1 || head[1] == 2) && head[2] < (1u << 24);
    if (good && head[1] == 2) good = all(&names_size, sizeof names_size) && names_size < (1u << 28);
    if (good) {
        entries.resize(head[2]);
        good = entries.empty() || all(entries.data(), entries.size() * sizeof(Entry));
    }
    if (good && names_size) { names.resize(names_size); good = all(names.data(), names.size()); }
    if (!good) { ::close(handle); return false; }
    file_ = handle;
    version_ = head[1];
    entries_ = std::move(entries);
    folders_.clear();
    folders_[""];   // the folder itself
    for (const Entry& entry : entries_) {
        if (version_ != 2 || entry.name == kNoName || entry.name >= names.size()) continue;
        const std::string_view path(names.c_str() + entry.name);
        // The file in its folder, and each folder in the one above it.
        size_t slash = path.rfind('/');
        const std::string folder(slash == std::string_view::npos ? std::string_view() : path.substr(0, slash));
        folders_[folder].push_back({std::string(slash == std::string_view::npos ? path : path.substr(slash + 1)), false, entry.size});
        for (std::string_view inner = folder; !inner.empty();) {
            slash = inner.rfind('/');
            const std::string_view above = slash == std::string_view::npos ? std::string_view() : inner.substr(0, slash);
            const std::string_view name = slash == std::string_view::npos ? inner : inner.substr(slash + 1);
            auto& children = folders_[std::string(above)];
            if (std::any_of(children.begin(), children.end(), [&](const Child& c) { return c.directory && c.name == name; })) break;
            children.push_back({std::string(name), true, 0});
            folders_[std::string(inner)];
            inner = above;
        }
    }
    return true;
}

const Entry* Pack::find(std::string_view path) const {
    const uint64_t hash = fnv1a(path);
    const auto found = std::lower_bound(entries_.begin(), entries_.end(), hash, [](const Entry& e, uint64_t h) { return e.hash < h; });
    return found == entries_.end() || found->hash != hash ? nullptr : &*found;
}

const std::vector<Pack::Child>* Pack::list(std::string_view path) const {
    const auto found = folders_.find(path);
    return found == folders_.end() ? nullptr : &found->second;
}

#ifdef __SWITCH__
bool read_at(void* file, uint64_t from, void* out, size_t size);   // below: fsFileRead
#endif

// A read that took 50 ms or more says whether it waited for another thread's read or for
// the card (SRW64_HD_PACK_SLOW): reads of 44 KB to 830 KB took 330-536 ms on a Switch.
bool Pack::read(const Entry& entry, uint64_t at, void* out, size_t size) const {
    if ((file_ < 0 && !offset_file_) || at > entry.size || size > entry.size - at) return false;
    using Clock = std::chrono::steady_clock;
    const auto started = Clock::now();
    const int64_t from = int64_t(entry.offset + at);
    bool good = true;
    Clock::time_point locked = started;
#ifdef __SWITCH__
    if (offset_file_) good = read_at(offset_file_, uint64_t(from), out, size);
    else
#endif
    {
        std::lock_guard lock(mutex_);
        locked = Clock::now();
        good = int64_t(SRW64_PACK_SEEK(file_, from, SEEK_SET)) == from;
        for (size_t done = 0; good && done < size;) {
            const ssize_t got = ::read(file_, static_cast<char*>(out) + done, size - done);
            if (got <= 0) good = false;
            else done += size_t(got);
        }
    }
    const auto ms = [](Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    if (const double total = ms(started, Clock::now()); total >= 50)
        std::fprintf(stderr, "SRW64_HD_PACK_SLOW %.0f ms (waited %.0f, read %.0f) %zu bytes at %llu%s\n", total, ms(started, locked),
                     total - ms(started, locked), size, (unsigned long long)from, offset_file_ ? " by offset" : "");
    return good;
}
}  // namespace srw64::hd_pack

#ifdef __SWITCH__
#include <cstdlib>
#include <filesystem>
#include <switch.h>
#include <sys/iosupport.h>
#include <sys/stat.h>
std::string srw64_switch_program_path();   // switch/switch_platform.cpp

namespace srw64::hd_pack {
bool read_at(void* file, uint64_t from, void* out, size_t size) {
    u64 got = 0;
    for (size_t done = 0; done < size; done += size_t(got)) {
        if (R_FAILED(fsFileRead(static_cast<FsFile*>(file), s64(from + done), static_cast<char*>(out) + done, size - done, FsReadOption_None, &got)) || !got)
            return false;
    }
    return true;
}
void close_offset_file(void* file) {
    fsFileClose(static_cast<FsFile*>(file));
    delete static_cast<FsFile*>(file);
}
// The pack's reads through the file system service at an offset: each a request of its own,
// so threads read at once with no lock and no shared position (the descriptor needed both:
// a seek and a read under one lock, and a long read held every other back).
bool Pack::read_at_offsets(const std::string& path) {
    FsFileSystem* card = fsdevGetDeviceFileSystem("sdmc");
    if (!card || file_ < 0) return false;
    auto* file = new FsFile;
    if (R_FAILED(fsFsOpenFile(card, path.c_str(), FsOpenMode_Read, file))) { delete file; return false; }
    offset_file_ = file;
    return true;
}
namespace {
Pack shared;
std::string legacy_root;   // a version 1 pack's folder with its slash: its pictures, the rest loose
constexpr const char* kDevice = "hd";

// "hd:/art/x.png" as "art/x.png".
std::string inside(const char* path) {
    const char* colon = std::strchr(path, ':');
    return normal(colon ? colon + 1 : path);
}
void fill(struct stat* st, bool directory, uint32_t size) {
    std::memset(st, 0, sizeof *st);
    st->st_mode = directory ? (S_IFDIR | 0555) : (S_IFREG | 0444);
    st->st_nlink = 1;
    st->st_size = size;
    st->st_blksize = 512;
    st->st_blocks = (size + 511) / 512;
}
// An open file reads ahead: stdio and the C++ streams read 1 KB at a time here (newlib's
// BUFSIZ), a request to the card each, for a 1 MB picture a thousand of them. A window of
// the entry is read at once and the small reads are served from it.
constexpr size_t kWindow = 256 * 1024;
struct File { const Entry* entry; uint64_t at; char* window; uint64_t window_at; size_t window_size; };
struct Dir { const std::vector<Pack::Child>* children; size_t at; };

int open_r(struct _reent* r, void* file, const char* path, int flags, int) {
    if ((flags & O_ACCMODE) != O_RDONLY) { r->_errno = EROFS; return -1; }
    const auto name = inside(path);
    const Entry* entry = shared.find(name);
    if (!entry) { r->_errno = shared.list(name) ? EISDIR : ENOENT; return -1; }
    *static_cast<File*>(file) = {entry, 0, nullptr, 0, 0};
    return 0;
}
int close_r(struct _reent*, void* file) {
    std::free(static_cast<File*>(file)->window);
    static_cast<File*>(file)->window = nullptr;
    return 0;
}
ssize_t read_r(struct _reent* r, void* file, char* out, size_t size) {
    auto* f = static_cast<File*>(file);
    const size_t left = f->entry->size - size_t(std::min<uint64_t>(f->at, f->entry->size));
    size = std::min(size, left);
    if (!size) return 0;
    if (size >= kWindow / 2) {   // a large read goes straight through
        if (!shared.read(*f->entry, f->at, out, size)) { r->_errno = EIO; return -1; }
        f->at += size;
        return ssize_t(size);
    }
    if (f->at < f->window_at || f->at + size > f->window_at + f->window_size) {
        if (!f->window && !(f->window = static_cast<char*>(std::malloc(kWindow)))) { r->_errno = ENOMEM; return -1; }
        f->window_at = f->at;
        f->window_size = size_t(std::min<uint64_t>(kWindow, f->entry->size - f->at));
        if (!shared.read(*f->entry, f->window_at, f->window, f->window_size)) { f->window_size = 0; r->_errno = EIO; return -1; }
    }
    std::memcpy(out, f->window + (f->at - f->window_at), size);
    f->at += size;
    return ssize_t(size);
}
off_t seek_r(struct _reent* r, void* file, off_t position, int whence) {
    auto* f = static_cast<File*>(file);
    const int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? int64_t(f->at) : whence == SEEK_END ? int64_t(f->entry->size) : -1;
    if (base < 0 || base + position < 0) { r->_errno = EINVAL; return -1; }
    f->at = uint64_t(base + position);
    return off_t(f->at);
}
int fstat_r(struct _reent*, void* file, struct stat* st) {
    fill(st, false, static_cast<File*>(file)->entry->size);
    return 0;
}
int stat_r(struct _reent* r, const char* path, struct stat* st) {
    const auto name = inside(path);
    if (const Entry* entry = shared.find(name)) { fill(st, false, entry->size); return 0; }
    if (shared.list(name)) { fill(st, true, 0); return 0; }
    r->_errno = ENOENT;
    return -1;
}
DIR_ITER* diropen_r(struct _reent* r, DIR_ITER* state, const char* path) {
    const auto* children = shared.list(inside(path));
    if (!children) { r->_errno = ENOENT; return nullptr; }
    *static_cast<Dir*>(state->dirStruct) = {children, 0};
    return state;
}
int dirreset_r(struct _reent*, DIR_ITER* state) {
    static_cast<Dir*>(state->dirStruct)->at = 0;
    return 0;
}
int dirnext_r(struct _reent* r, DIR_ITER* state, char* filename, struct stat* st) {
    auto* d = static_cast<Dir*>(state->dirStruct);
    if (d->at >= d->children->size()) { r->_errno = ENOENT; return -1; }
    const Pack::Child& child = (*d->children)[d->at++];
    std::snprintf(filename, NAME_MAX, "%s", child.name.c_str());
    if (st) fill(st, child.directory, child.size);
    return 0;
}
int dirclose_r(struct _reent*, DIR_ITER*) { return 0; }
int read_only(struct _reent* r) { r->_errno = EROFS; return -1; }

devoptab_t make_device() {
    devoptab_t d{};
    d.name = kDevice;
    d.structSize = sizeof(File);
    d.open_r = open_r;
    d.close_r = close_r;
    d.write_r = [](struct _reent* r, void*, const char*, size_t) -> ssize_t { return read_only(r); };
    d.read_r = read_r;
    d.seek_r = seek_r;
    d.fstat_r = fstat_r;
    d.stat_r = stat_r;
    d.lstat_r = stat_r;
    d.link_r = [](struct _reent* r, const char*, const char*) { return read_only(r); };
    d.unlink_r = [](struct _reent* r, const char*) { return read_only(r); };
    d.chdir_r = [](struct _reent* r, const char*) { r->_errno = ENOSYS; return -1; };
    d.rename_r = [](struct _reent* r, const char*, const char*) { return read_only(r); };
    d.mkdir_r = [](struct _reent* r, const char*, int) { return read_only(r); };
    d.rmdir_r = [](struct _reent* r, const char*) { return read_only(r); };
    d.dirStateSize = sizeof(Dir);
    d.diropen_r = diropen_r;
    d.dirreset_r = dirreset_r;
    d.dirnext_r = dirnext_r;
    d.dirclose_r = dirclose_r;
    return d;
}
}  // namespace

std::string mount() {
    static const std::string art = [] {
        std::error_code missing;
        const auto beside = std::filesystem::path(srw64_switch_program_path()).parent_path() / "hd" / "art.pack";
        for (const std::string candidate : {std::string("romfs:/hd/art.pack"), beside.string()}) {
            if (!std::filesystem::exists(candidate, missing)) continue;
            if (!shared.open(candidate)) {
                std::fprintf(stderr, "SRW64_HD_PACK_BAD %s\n", candidate.c_str());
                continue;
            }
            // On the card: reads at an offset with no lock (above); in the NRO's romfs, the descriptor.
            const bool offsets = candidate.rfind("romfs:", 0) != 0 && shared.read_at_offsets(candidate);
            if (shared.version() == 1) {
                // The pictures only; the folder's other files are read from the card as before.
                legacy_root = std::filesystem::path(candidate).parent_path().string() + "/";
                std::fprintf(stderr, "SRW64_HD_PACK %zu pictures (version 1, the other files loose)\n", shared.files());
                return std::string();
            }
            static const devoptab_t device = make_device();
            if (AddDevice(&device) < 0) {
                std::fprintf(stderr, "SRW64_HD_PACK_BAD %s: the device could not be added\n", candidate.c_str());
                return std::string();
            }
            std::fprintf(stderr, "SRW64_HD_PACK %zu files, mounted as %s:%s\n", shared.files(), kDevice, offsets ? ", read at offsets" : "");
            return std::string(kDevice) + ":/art";
        }
        return std::string();
    }();
    return art;
}

bool read_file(const std::string& path, std::string& out) {
    mount();
    if (!shared.opened()) return false;
    std::string name;
    if (shared.version() == 2) {
        if (path.rfind(std::string(kDevice) + ":", 0) != 0) return false;
        name = inside(path.c_str());
    } else {
        if (legacy_root.empty() || path.rfind(legacy_root, 0) != 0) return false;
        name = path.substr(legacy_root.size());
    }
    const Entry* entry = shared.find(name);
    if (!entry) return false;
    out.resize(entry->size);
    return shared.read(*entry, 0, out.data(), out.size());
}
}  // namespace srw64::hd_pack
#else
namespace srw64::hd_pack {
bool read_file(const std::string&, std::string&) { return false; }
}
#endif
