//===- llvm/Support/Unix/Path.inc - Unix Path Implementation ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Unix specific implementation of the Path API.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only generic UNIX code that
//===          is guaranteed to work on *all* UNIX variants.
//===----------------------------------------------------------------------===//

#include "Unix.h"
#include <limits.h>
#include <stdio.h>
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#if HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif
#if HAVE_DIRENT_H
# include <dirent.h>
# define NAMLEN(dirent) strlen((dirent)->d_name)
#else
# define dirent direct
# define NAMLEN(dirent) (dirent)->d_namlen
# if HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# if HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# if HAVE_NDIR_H
#  include <ndir.h>
# endif
#endif

#include <pwd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <sys/attr.h>
#endif

// Both stdio.h and cstdio are included via different paths and
// stdcxx's cstdio doesn't include stdio.h, so it doesn't #undef the macros
// either.
#undef ferror
#undef feof

// For GNU Hurd
#if defined(__GNU__) && !defined(PATH_MAX)
# define PATH_MAX 4096
#endif

#include <sys/types.h>
#if !defined(__APPLE__) && !defined(__OpenBSD__) && !defined(__FreeBSD__) &&   \
    !defined(__linux__)
#include <sys/statvfs.h>
#define STATVFS statvfs
#define FSTATVFS fstatvfs
#define STATVFS_F_FRSIZE(vfs) vfs.f_frsize
#else
#if defined(__OpenBSD__) || defined(__FreeBSD__)
#include <sys/mount.h>
#include <sys/param.h>
#elif defined(__linux__)
#if defined(HAVE_LINUX_MAGIC_H)
#include <linux/magic.h>
#else
#if defined(HAVE_LINUX_NFS_FS_H)
#include <linux/nfs_fs.h>
#endif
#if defined(HAVE_LINUX_SMB_H)
#include <linux/smb.h>
#endif
#endif
#include <sys/vfs.h>
#else
#include <sys/mount.h>
#endif
#define STATVFS statfs
#define FSTATVFS fstatfs
#define STATVFS_F_FRSIZE(vfs) static_cast<uint64_t>(vfs.f_bsize)
#endif

#if defined(__NetBSD__)
#define STATVFS_F_FLAG(vfs) (vfs).f_flag
#else
#define STATVFS_F_FLAG(vfs) (vfs).f_flags
#endif

using namespace llvm;

namespace llvm {
namespace sys  {
namespace fs {
#if defined(__FreeBSD__) || defined (__NetBSD__) || defined(__Bitrig__) || \
    defined(__OpenBSD__) || defined(__minix) || defined(__FreeBSD_kernel__) || \
    defined(__linux__) || defined(__CYGWIN__) || defined(__DragonFly__) || \
    defined(_AIX)
static int
test_dir(char ret[PATH_MAX], const char *dir, const char *bin)
{
  struct stat sb;
  char fullpath[PATH_MAX];

  snprintf(fullpath, PATH_MAX, "%s/%s", dir, bin);
  if (!realpath(fullpath, ret))
    return 1;
  if (stat(fullpath, &sb) != 0)
    return 1;

  return 0;
}

static char *
getprogpath(char ret[PATH_MAX], const char *bin)
{
  char *pv, *s, *t;

  /* First approach: absolute path. */
  if (bin[0] == '/') {
    if (test_dir(ret, "/", bin) == 0)
      return ret;
    return nullptr;
  }

  /* Second approach: relative path. */
  if (strchr(bin, '/')) {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, PATH_MAX))
      return nullptr;
    if (test_dir(ret, cwd, bin) == 0)
      return ret;
    return nullptr;
  }

  /* Third approach: $PATH */
  if ((pv = getenv("PATH")) == nullptr)
    return nullptr;
  s = pv = strdup(pv);
  if (!pv)
    return nullptr;
  while ((t = strsep(&s, ":")) != nullptr) {
    if (test_dir(ret, t, bin) == 0) {
      free(pv);
      return ret;
    }
  }
  free(pv);
  return nullptr;
}
#endif // __FreeBSD__ || __NetBSD__ || __FreeBSD_kernel__

/// GetMainExecutable - Return the path to the main executable, given the
/// value of argv[0] from program startup.
std::string getMainExecutable(const char *argv0, void *MainAddr) {
#if defined(__APPLE__)
  // On OS X the executable path is saved to the stack by dyld. Reading it
  // from there is much faster than calling dladdr, especially for large
  // binaries with symbols.
  char exe_path[MAXPATHLEN];
  uint32_t size = sizeof(exe_path);
  if (_NSGetExecutablePath(exe_path, &size) == 0) {
    char link_path[MAXPATHLEN];
    if (realpath(exe_path, link_path))
      return link_path;
  }
#elif defined(__FreeBSD__) || defined (__NetBSD__) || defined(__Bitrig__) || \
      defined(__OpenBSD__) || defined(__minix) || defined(__DragonFly__) || \
      defined(__FreeBSD_kernel__) || defined(_AIX)
  char exe_path[PATH_MAX];

  if (getprogpath(exe_path, argv0) != NULL)
    return exe_path;
#elif defined(__linux__) || defined(__CYGWIN__)
  char exe_path[MAXPATHLEN];
  StringRef aPath("/proc/self/exe");
  if (sys::fs::exists(aPath)) {
      // /proc is not always mounted under Linux (chroot for example).
      ssize_t len = readlink(aPath.str().c_str(), exe_path, sizeof(exe_path));
      if (len >= 0)
          return std::string(exe_path, len);
  } else {
      // Fall back to the classical detection.
      if (getprogpath(exe_path, argv0))
        return exe_path;
  }
#elif defined(HAVE_DLFCN_H) && defined(HAVE_DLADDR)
  // Use dladdr to get executable path if available.
  Dl_info DLInfo;
  int err = dladdr(MainAddr, &DLInfo);
  if (err == 0)
    return "";

  // If the filename is a symlink, we need to resolve and return the location of
  // the actual executable.
  char link_path[MAXPATHLEN];
  if (realpath(DLInfo.dli_fname, link_path))
    return link_path;
#else
#error GetMainExecutable is not implemented on this host yet.
#endif
  return "";
}

TimePoint<> file_status::getLastAccessedTime() const {
  return toTimePoint(fs_st_atime);
}

TimePoint<> file_status::getLastModificationTime() const {
  return toTimePoint(fs_st_mtime);
}

UniqueID file_status::getUniqueID() const {
  return UniqueID(fs_st_dev, fs_st_ino);
}

uint32_t file_status::getLinkCount() const {
  return fs_st_nlinks;
}

ErrorOr<space_info> disk_space(const Twine &Path) {
  struct STATVFS Vfs;
  if (::STATVFS(Path.str().c_str(), &Vfs))
    return std::error_code(errno, std::generic_category());
  auto FrSize = STATVFS_F_FRSIZE(Vfs);
  space_info SpaceInfo;
  SpaceInfo.capacity = static_cast<uint64_t>(Vfs.f_blocks) * FrSize;
  SpaceInfo.free = static_cast<uint64_t>(Vfs.f_bfree) * FrSize;
  SpaceInfo.available = static_cast<uint64_t>(Vfs.f_bavail) * FrSize;
  return SpaceInfo;
}

std::error_code current_path(SmallVectorImpl<char> &result) {
  result.clear();

  const char *pwd = ::getenv("PWD");
  llvm::sys::fs::file_status PWDStatus, DotStatus;
  if (pwd && llvm::sys::path::is_absolute(pwd) &&
      !llvm::sys::fs::status(pwd, PWDStatus) &&
      !llvm::sys::fs::status(".", DotStatus) &&
      PWDStatus.getUniqueID() == DotStatus.getUniqueID()) {
    result.append(pwd, pwd + strlen(pwd));
    return std::error_code();
  }

#ifdef MAXPATHLEN
  result.reserve(MAXPATHLEN);
#else
// For GNU Hurd
  result.reserve(1024);
#endif

  while (true) {
    if (::getcwd(result.data(), result.capacity()) == nullptr) {
      // See if there was a real error.
      if (errno != ENOMEM)
        return std::error_code(errno, std::generic_category());
      // Otherwise there just wasn't enough space.
      result.reserve(result.capacity() * 2);
    } else
      break;
  }

  result.set_size(strlen(result.data()));
  return std::error_code();
}

std::error_code set_current_path(const Twine &path) {
  SmallString<128> path_storage;
  StringRef p = path.toNullTerminatedStringRef(path_storage);

  if (::chdir(p.begin()) == -1)
    return std::error_code(errno, std::generic_category());

  return std::error_code();
}

std::error_code create_directory(const Twine &path, bool IgnoreExisting,
                                 perms Perms) {
  SmallString<128> path_storage;
  StringRef p = path.toNullTerminatedStringRef(path_storage);

  if (::mkdir(p.begin(), Perms) == -1) {
    if (errno != EEXIST || !IgnoreExisting)
      return std::error_code(errno, std::generic_category());
  }

  return std::error_code();
}

// Note that we are using symbolic link because hard links are not supported by
// all filesystems (SMB doesn't).
std::error_code create_link(const Twine &to, const Twine &from) {
  // Get arguments.
  SmallString<128> from_storage;
  SmallString<128> to_storage;
  StringRef f = from.toNullTerminatedStringRef(from_storage);
  StringRef t = to.toNullTerminatedStringRef(to_storage);

  if (::symlink(t.begin(), f.begin()) == -1)
    return std::error_code(errno, std::generic_category());

  return std::error_code();
}

std::error_code create_hard_link(const Twine &to, const Twine &from) {
  // Get arguments.
  SmallString<128> from_storage;
  SmallString<128> to_storage;
  StringRef f = from.toNullTerminatedStringRef(from_storage);
  StringRef t = to.toNullTerminatedStringRef(to_storage);

  if (::link(t.begin(), f.begin()) == -1)
    return std::error_code(errno, std::generic_category());

  return std::error_code();
}

std::error_code remove(const Twine &path, bool IgnoreNonExisting) {
  SmallString<128> path_storage;
  StringRef p = path.toNullTerminatedStringRef(path_storage);

  struct stat buf;
  if (lstat(p.begin(), &buf) != 0) {
    if (errno != ENOENT || !IgnoreNonExisting)
      return std::error_code(errno, std::generic_category());
    return std::error_code();
  }

  // Note: this check catches strange situations. In all cases, LLVM should
  // only be involved in the creation and deletion of regular files.  This
  // check ensures that what we're trying to erase is a regular file. It
  // effectively prevents LLVM from erasing things like /dev/null, any block
  // special file, or other things that aren't "regular" files.
  if (!S_ISREG(buf.st_mode) && !S_ISDIR(buf.st_mode) && !S_ISLNK(buf.st_mode))
    return make_error_code(errc::operation_not_permitted);

  if (::remove(p.begin()) == -1) {
    if (errno != ENOENT || !IgnoreNonExisting)
      return std::error_code(errno, std::generic_category());
  }

  return std::error_code();
}

static bool is_local_impl(struct STATVFS &Vfs) {
#if defined(__linux__)
#ifndef NFS_SUPER_MAGIC
#define NFS_SUPER_MAGIC 0x6969
#endif
#ifndef SMB_SUPER_MAGIC
#define SMB_SUPER_MAGIC 0x517B
#endif
#ifndef CIFS_MAGIC_NUMBER
#define CIFS_MAGIC_NUMBER 0xFF534D42
#endif
  switch ((uint32_t)Vfs.f_type) {
  case NFS_SUPER_MAGIC:
  case SMB_SUPER_MAGIC:
  case CIFS_MAGIC_NUMBER:
    return false;
  default:
    return true;
  }
#elif defined(__CYGWIN__)
  // Cygwin doesn't expose this information; would need to use Win32 API.
  return false;
#elif defined(__sun)
  // statvfs::f_basetype contains a null-terminated FSType name of the mounted target
  StringRef fstype(Vfs.f_basetype);
  // NFS is the only non-local fstype??
  return !fstype.equals("nfs");
#else
  return !!(STATVFS_F_FLAG(Vfs) & MNT_LOCAL);
#endif
}

std::error_code is_local(const Twine &Path, bool &Result) {
  struct STATVFS Vfs;
  if (::STATVFS(Path.str().c_str(), &Vfs))
    return std::error_code(errno, std::generic_category());

  Result = is_local_impl(Vfs);
  return std::error_code();
}

std::error_code is_local(int FD, bool &Result) {
  struct STATVFS Vfs;
  if (::FSTATVFS(FD, &Vfs))
    return std::error_code(errno, std::generic_category());

  Result = is_local_impl(Vfs);
  return std::error_code();
}

std::error_code rename(const Twine &from, const Twine &to) {
  // Get arguments.
  SmallString<128> from_storage;
  SmallString<128> to_storage;
  StringRef f = from.toNullTerminatedStringRef(from_storage);
  StringRef t = to.toNullTerminatedStringRef(to_storage);

  if (::rename(f.begin(), t.begin()) == -1)
    return std::error_code(errno, std::generic_category());

  return std::error_code();
}

std::error_code resize_file(int FD, uint64_t Size) {
#if defined(HAVE_POSIX_FALLOCATE)
  // If we have posix_fallocate use it. Unlike ftruncate it always allocates
  // space, so we get an error if the disk is full.
  if (int Err = ::posix_fallocate(FD, 0, Size)) {
    if (Err != EOPNOTSUPP)
      return std::error_code(Err, std::generic_category());
  }
#endif
  // Use ftruncate as a fallback. It may or may not allocate space. At least on
  // OS X with HFS+ it does.
  if (::ftruncate(FD, Size) == -1)
    return std::error_code(errno, std::generic_category());

  return std::error_code();
}

static int convertAccessMode(AccessMode Mode) {
  switch (Mode) {
  case AccessMode::Exist:
    return F_OK;
  case AccessMode::Write:
    return W_OK;
  case AccessMode::Execute:
    return R_OK | X_OK; // scripts also need R_OK.
  }
  llvm_unreachable("invalid enum");
}

std::error_code access(const Twine &Path, AccessMode Mode) {
  SmallString<128> PathStorage;
  StringRef P = Path.toNullTerminatedStringRef(PathStorage);

  if (::access(P.begin(), convertAccessMode(Mode)) == -1)
    return std::error_code(errno, std::generic_category());

  if (Mode == AccessMode::Execute) {
    // Don't say that directories are executable.
    struct stat buf;
    if (0 != stat(P.begin(), &buf))
      return errc::permission_denied;
    if (!S_ISREG(buf.st_mode))
      return errc::permission_denied;
  }

  return std::error_code();
}

bool can_execute(const Twine &Path) {
  return !access(Path, AccessMode::Execute);
}

bool equivalent(file_status A, file_status B) {
  assert(status_known(A) && status_known(B));
  return A.fs_st_dev == B.fs_st_dev &&
         A.fs_st_ino == B.fs_st_ino;
}

std::error_code equivalent(const Twine &A, const Twine &B, bool &result) {
  file_status fsA, fsB;
  if (std::error_code ec = status(A, fsA))
    return ec;
  if (std::error_code ec = status(B, fsB))
    return ec;
  result = equivalent(fsA, fsB);
  return std::error_code();
}

static void expandTildeExpr(SmallVectorImpl<char> &Path) {
  StringRef PathStr(Path.begin(), Path.size());
  if (PathStr.empty() || !PathStr.startswith("~"))
    return;

  PathStr = PathStr.drop_front();
  StringRef Expr =
      PathStr.take_until([](char c) { return path::is_separator(c); });
  StringRef Remainder = PathStr.substr(Expr.size() + 1);
  SmallString<128> Storage;
  if (Expr.empty()) {
    // This is just ~/..., resolve it to the current user's home dir.
    if (!path::home_directory(Storage)) {
      // For some reason we couldn't get the home directory.  Just exit.
      return;
    }

    // Overwrite the first character and insert the rest.
    Path[0] = Storage[0];
    Path.insert(Path.begin() + 1, Storage.begin() + 1, Storage.end());
    return;
  }

  // This is a string of the form ~username/, look up this user's entry in the
  // password database.
  struct passwd *Entry = nullptr;
  std::string User = Expr.str();
  Entry = ::getpwnam(User.c_str());

  if (!Entry) {
    // Unable to look up the entry, just return back the original path.
    return;
  }

  Storage = Remainder;
  Path.clear();
  Path.append(Entry->pw_dir, Entry->pw_dir + strlen(Entry->pw_dir));
  llvm::sys::path::append(Path, Storage);
}

static std::error_code fillStatus(int StatRet, const struct stat &Status,
                             file_status &Result) {
  if (StatRet != 0) {
    std::error_code ec(errno, std::generic_category());
    if (ec == errc::no_such_file_or_directory)
      Result = file_status(file_type::file_not_found);
    else
      Result = file_status(file_type::status_error);
    return ec;
  }

  file_type Type = file_type::type_unknown;

  if (S_ISDIR(Status.st_mode))
    Type = file_type::directory_file;
  else if (S_ISREG(Status.st_mode))
    Type = file_type::regular_file;
  else if (S_ISBLK(Status.st_mode))
    Type = file_type::block_file;
  else if (S_ISCHR(Status.st_mode))
    Type = file_type::character_file;
  else if (S_ISFIFO(Status.st_mode))
    Type = file_type::fifo_file;
  else if (S_ISSOCK(Status.st_mode))
    Type = file_type::socket_file;
  else if (S_ISLNK(Status.st_mode))
    Type = file_type::symlink_file;

  perms Perms = static_cast<perms>(Status.st_mode) & all_perms;
  Result = file_status(Type, Perms, Status.st_dev, Status.st_nlink,
                       Status.st_ino, Status.st_atime, Status.st_mtime,
                       Status.st_uid, Status.st_gid, Status.st_size);

  return std::error_code();
}

std::error_code status(const Twine &Path, file_status &Result, bool Follow) {
  SmallString<128> PathStorage;
  StringRef P = Path.toNullTerminatedStringRef(PathStorage);

  struct stat Status;
  int StatRet = (Follow ? ::stat : ::lstat)(P.begin(), &Status);
  return fillStatus(StatRet, Status, Result);
}

std::error_code status(int FD, file_status &Result) {
  struct stat Status;
  int StatRet = ::fstat(FD, &Status);
  return fillStatus(StatRet, Status, Result);
}

std::error_code setPermissions(const Twine &Path, perms Permissions) {
  SmallString<128> PathStorage;
  StringRef P = Path.toNullTerminatedStringRef(PathStorage);

  if (::chmod(P.begin(), Permissions))
    return std::error_code(errno, std::generic_category());
  return std::error_code();
}

std::error_code setLastModificationAndAccessTime(int FD, TimePoint<> Time) {
#if defined(HAVE_FUTIMENS)
  timespec Times[2];
  Times[0] = Times[1] = sys::toTimeSpec(Time);
  if (::futimens(FD, Times))
    return std::error_code(errno, std::generic_category());
  return std::error_code();
#elif defined(HAVE_FUTIMES)
  timeval Times[2];
  Times[0] = Times[1] = sys::toTimeVal(
      std::chrono::time_point_cast<std::chrono::microseconds>(Time));
  if (::futimes(FD, Times))
    return std::error_code(errno, std::generic_category());
  return std::error_code();
#else
#warning Missing futimes() and futimens()
  return make_error_code(errc::function_not_supported);
#endif
}

std::error_code mapped_file_region::init(int FD, uint64_t Offset,
                                         mapmode Mode) {
  assert(Size != 0);

  int flags = (Mode == readwrite) ? MAP_SHARED : MAP_PRIVATE;
  int prot = (Mode == readonly) ? PROT_READ : (PROT_READ | PROT_WRITE);
#if defined(__APPLE__)
  //----------------------------------------------------------------------
  // Newer versions of MacOSX have a flag that will allow us to read from
  // binaries whose code signature is invalid without crashing by using
  // the MAP_RESILIENT_CODESIGN flag. Also if a file from removable media
  // is mapped we can avoid crashing and return zeroes to any pages we try
  // to read if the media becomes unavailable by using the
  // MAP_RESILIENT_MEDIA flag.  These flags are only usable when mapping
  // with PROT_READ, so take care not to specify them otherwise.
  //----------------------------------------------------------------------
  if (Mode == readonly) {
#if defined(MAP_RESILIENT_CODESIGN)
    flags |= MAP_RESILIENT_CODESIGN;
#endif
#if defined(MAP_RESILIENT_MEDIA)
    flags |= MAP_RESILIENT_MEDIA;
#endif
  }
#endif // #if defined (__APPLE__)

  Mapping = ::mmap(nullptr, Size, prot, flags, FD, Offset);
  if (Mapping == MAP_FAILED)
    return std::error_code(errno, std::generic_category());
  return std::error_code();
}

mapped_file_region::mapped_file_region(int fd, mapmode mode, uint64_t length,
                                       uint64_t offset, std::error_code &ec)
    : Size(length), Mapping() {
  // Make sure that the requested size fits within SIZE_T.
  if (length > std::numeric_limits<size_t>::max()) {
    ec = make_error_code(errc::invalid_argument);
    return;
  }

  ec = init(fd, offset, mode);
  if (ec)
    Mapping = nullptr;
}

mapped_file_region::~mapped_file_region() {
  if (Mapping)
    ::munmap(Mapping, Size);
}

uint64_t mapped_file_region::size() const {
  assert(Mapping && "Mapping failed but used anyway!");
  return Size;
}

char *mapped_file_region::data() const {
  assert(Mapping && "Mapping failed but used anyway!");
  return reinterpret_cast<char*>(Mapping);
}

const char *mapped_file_region::const_data() const {
  assert(Mapping && "Mapping failed but used anyway!");
  return reinterpret_cast<const char*>(Mapping);
}

int mapped_file_region::alignment() {
  return Process::getPageSize();
}

std::error_code detail::directory_iterator_construct(detail::DirIterState &it,
                                                     StringRef path,
                                                     bool follow_symlinks) {
  SmallString<128> path_null(path);
  DIR *directory = ::opendir(path_null.c_str());
  if (!directory)
    return std::error_code(errno, std::generic_category());

  it.IterationHandle = reinterpret_cast<intptr_t>(directory);
  // Add something for replace_filename to replace.
  path::append(path_null, ".");
  it.CurrentEntry = directory_entry(path_null.str(), follow_symlinks);
  return directory_iterator_increment(it);
}

std::error_code detail::directory_iterator_destruct(detail::DirIterState &it) {
  if (it.IterationHandle)
    ::closedir(reinterpret_cast<DIR *>(it.IterationHandle));
  it.IterationHandle = 0;
  it.CurrentEntry = directory_entry();
  return std::error_code();
}

std::error_code detail::directory_iterator_increment(detail::DirIterState &it) {
  errno = 0;
  dirent *cur_dir = ::readdir(reinterpret_cast<DIR *>(it.IterationHandle));
  if (cur_dir == nullptr && errno != 0) {
    return std::error_code(errno, std::generic_category());
  } else if (cur_dir != nullptr) {
    StringRef name(cur_dir->d_name, NAMLEN(cur_dir));
    if ((name.size() == 1 && name[0] == '.') ||
        (name.size() == 2 && name[0] == '.' && name[1] == '.'))
      return directory_iterator_increment(it);
    it.CurrentEntry.replace_filename(name);
  } else
    return directory_iterator_destruct(it);

  return std::error_code();
}

#if !defined(F_GETPATH)
static bool hasProcSelfFD() {
  // If we have a /proc filesystem mounted, we can quickly establish the
  // real name of the file with readlink
  static const bool Result = (::access("/proc/self/fd", R_OK) == 0);
  return Result;
}
#endif

std::error_code openFileForRead(const Twine &Name, int &ResultFD,
                                SmallVectorImpl<char> *RealPath) {
  SmallString<128> Storage;
  StringRef P = Name.toNullTerminatedStringRef(Storage);
  int OpenFlags = O_RDONLY;
#ifdef O_CLOEXEC
  OpenFlags |= O_CLOEXEC;
#endif
  if ((ResultFD = open(P.begin(), OpenFlags)) < 0)
    return std::error_code(errno, std::generic_category());
  //if ((ResultFD = sys::RetryAfterSignal(-1, open, P.begin(), OpenFlags)) < 0)
  //  return std::error_code(errno, std::generic_category());
#ifndef O_CLOEXEC
  int r = fcntl(ResultFD, F_SETFD, FD_CLOEXEC);
  (void)r;
  assert(r == 0 && "fcntl(F_SETFD, FD_CLOEXEC) failed");
#endif
  // Attempt to get the real name of the file, if the user asked
  if(!RealPath)
    return std::error_code();
  RealPath->clear();
#if defined(F_GETPATH)
  // When F_GETPATH is availble, it is the quickest way to get
  // the real path name.
  char Buffer[MAXPATHLEN];
  if (::fcntl(ResultFD, F_GETPATH, Buffer) != -1)
    RealPath->append(Buffer, Buffer + strlen(Buffer));
#else
  char Buffer[PATH_MAX];
  if (hasProcSelfFD()) {
    char ProcPath[64];
    snprintf(ProcPath, sizeof(ProcPath), "/proc/self/fd/%d", ResultFD);
    ssize_t CharCount = ::readlink(ProcPath, Buffer, sizeof(Buffer));
    if (CharCount > 0)
      RealPath->append(Buffer, Buffer + CharCount);
  } else {
    // Use ::realpath to get the real path name
    if (::realpath(P.begin(), Buffer) != nullptr)
      RealPath->append(Buffer, Buffer + strlen(Buffer));
  }
#endif
  return std::error_code();
}

std::error_code openFileForWrite(const Twine &Name, int &ResultFD,
                            sys::fs::OpenFlags Flags, unsigned Mode) {
  // Verify that we don't have both "append" and "excl".
  assert((!(Flags & sys::fs::F_Excl) || !(Flags & sys::fs::F_Append)) &&
         "Cannot specify both 'excl' and 'append' file creation flags!");

  int OpenFlags = O_CREAT;

#ifdef O_CLOEXEC
  OpenFlags |= O_CLOEXEC;
#endif

  if (Flags & F_RW)
    OpenFlags |= O_RDWR;
  else
    OpenFlags |= O_WRONLY;

  if (Flags & F_Append)
    OpenFlags |= O_APPEND;
  else
    OpenFlags |= O_TRUNC;

  if (Flags & F_Excl)
    OpenFlags |= O_EXCL;

  SmallString<128> Storage;
  StringRef P = Name.toNullTerminatedStringRef(Storage);
  if ((ResultFD = open(P.begin(), OpenFlags, Mode)) < 0)
    return std::error_code(errno, std::generic_category());
  //if ((ResultFD = sys::RetryAfterSignal(-1, open, P.begin(), OpenFlags, Mode)) < 0)
  //  return std::error_code(errno, std::generic_category());
#ifndef O_CLOEXEC
  int r = fcntl(ResultFD, F_SETFD, FD_CLOEXEC);
  (void)r;
  assert(r == 0 && "fcntl(F_SETFD, FD_CLOEXEC) failed");
#endif
  return std::error_code();
}

std::error_code getPathFromOpenFD(int FD, SmallVectorImpl<char> &ResultPath) {
  if (FD < 0)
    return make_error_code(errc::bad_file_descriptor);

#if defined(F_GETPATH)
  // When F_GETPATH is availble, it is the quickest way to get
  // the path from a file descriptor.
  ResultPath.reserve(MAXPATHLEN);
  if (::fcntl(FD, F_GETPATH, ResultPath.begin()) == -1)
    return std::error_code(errno, std::generic_category());

  ResultPath.set_size(strlen(ResultPath.begin()));
#else
  // If we have a /proc filesystem mounted, we can quickly establish the
  // real name of the file with readlink. Otherwise, we don't know how to
  // get the filename from a file descriptor. Give up.
  if (!fs::hasProcSelfFD())
    return make_error_code(errc::function_not_supported);

  ResultPath.reserve(PATH_MAX);
  char ProcPath[64];
  snprintf(ProcPath, sizeof(ProcPath), "/proc/self/fd/%d", FD);
  ssize_t CharCount = ::readlink(ProcPath, ResultPath.begin(), ResultPath.capacity());
  if (CharCount < 0)
      return std::error_code(errno, std::generic_category());

  // Was the filename truncated?
  if (static_cast<size_t>(CharCount) == ResultPath.capacity()) {
    // Use lstat to get the size of the filename
    struct stat sb;
    if (::lstat(ProcPath, &sb) < 0)
      return std::error_code(errno, std::generic_category());

    ResultPath.reserve(sb.st_size + 1);
    CharCount = ::readlink(ProcPath, ResultPath.begin(), ResultPath.capacity());
    if (CharCount < 0)
      return std::error_code(errno, std::generic_category());

    // Test for race condition: did the link size change?
    if (CharCount > sb.st_size)
      return std::error_code(ENAMETOOLONG, std::generic_category());
  }
  ResultPath.set_size(static_cast<size_t>(CharCount));
#endif
  return std::error_code();
}

template <typename T>
static std::error_code remove_directories_impl(const T &Entry,
                                               bool IgnoreErrors) {
  std::error_code EC;
  directory_iterator Begin(Entry, EC, false);
  directory_iterator End;
  while (Begin != End) {
    auto &Item = *Begin;
    file_status st;
    EC = Item.status(st);
    if (EC && !IgnoreErrors)
      return EC;

    if (is_directory(st)) {
      EC = remove_directories_impl(Item, IgnoreErrors);
      if (EC && !IgnoreErrors)
        return EC;
    }

    EC = fs::remove(Item.path(), true);
    if (EC && !IgnoreErrors)
      return EC;

    Begin.increment(EC);
    if (EC && !IgnoreErrors)
      return EC;
  }
  return std::error_code();
}

std::error_code remove_directories(const Twine &path, bool IgnoreErrors) {
  auto EC = remove_directories_impl(path, IgnoreErrors);
  if (EC && !IgnoreErrors)
    return EC;
  EC = fs::remove(path, true);
  if (EC && !IgnoreErrors)
    return EC;
  return std::error_code();
}

std::error_code real_path(const Twine &path, SmallVectorImpl<char> &dest,
                          bool expand_tilde) {
  dest.clear();
  if (path.isTriviallyEmpty())
    return std::error_code();

  if (expand_tilde) {
    SmallString<128> Storage;
    path.toVector(Storage);
    expandTildeExpr(Storage);
    return real_path(Storage, dest, false);
  }

  int fd;
  std::error_code EC = openFileForRead(path, fd, &dest);

  if (EC)
    return EC;
  ::close(fd);
  return std::error_code();
}

} // end namespace fs

namespace path {

bool home_directory(SmallVectorImpl<char> &result) {
  char *RequestedDir = getenv("HOME");
  if (!RequestedDir) {
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_dir)
      RequestedDir = pw->pw_dir;
  }
  if (!RequestedDir)
    return false;

  result.clear();
  result.append(RequestedDir, RequestedDir + strlen(RequestedDir));
  return true;
}

static bool getDarwinConfDir(bool TempDir, SmallVectorImpl<char> &Result) {
  #if defined(_CS_DARWIN_USER_TEMP_DIR) && defined(_CS_DARWIN_USER_CACHE_DIR)
  // On Darwin, use DARWIN_USER_TEMP_DIR or DARWIN_USER_CACHE_DIR.
  // macros defined in <unistd.h> on darwin >= 9
  int ConfName = TempDir ? _CS_DARWIN_USER_TEMP_DIR
                         : _CS_DARWIN_USER_CACHE_DIR;
  size_t ConfLen = confstr(ConfName, nullptr, 0);
  if (ConfLen > 0) {
    do {
      Result.resize(ConfLen);
      ConfLen = confstr(ConfName, Result.data(), Result.size());
    } while (ConfLen > 0 && ConfLen != Result.size());

    if (ConfLen > 0) {
      assert(Result.back() == 0);
      Result.pop_back();
      return true;
    }

    Result.clear();
  }
  #endif
  return false;
}

static bool getUserCacheDir(SmallVectorImpl<char> &Result) {
  // First try using XDG_CACHE_HOME env variable,
  // as specified in XDG Base Directory Specification at
  // http://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html
  if (const char *XdgCacheDir = std::getenv("XDG_CACHE_HOME")) {
    Result.clear();
    Result.append(XdgCacheDir, XdgCacheDir + strlen(XdgCacheDir));
    return true;
  }

  // Try Darwin configuration query
  if (getDarwinConfDir(false, Result))
    return true;

  // Use "$HOME/.cache" if $HOME is available
  if (home_directory(Result)) {
    append(Result, ".cache");
    return true;
  }

  return false;
}

static const char *getEnvTempDir() {
  // Check whether the temporary directory is specified by an environment
  // variable.
  const char *EnvironmentVariables[] = {"TMPDIR", "TMP", "TEMP", "TEMPDIR"};
  for (const char *Env : EnvironmentVariables) {
    if (const char *Dir = std::getenv(Env))
      return Dir;
  }

  return nullptr;
}

static const char *getDefaultTempDir(bool ErasedOnReboot) {
#ifdef P_tmpdir
  if ((bool)P_tmpdir)
    return P_tmpdir;
#endif

  if (ErasedOnReboot)
    return "/tmp";
  return "/var/tmp";
}

void system_temp_directory(bool ErasedOnReboot, SmallVectorImpl<char> &Result) {
  Result.clear();

  if (ErasedOnReboot) {
    // There is no env variable for the cache directory.
    if (const char *RequestedDir = getEnvTempDir()) {
      Result.append(RequestedDir, RequestedDir + strlen(RequestedDir));
      return;
    }
  }

  if (getDarwinConfDir(ErasedOnReboot, Result))
    return;

  const char *RequestedDir = getDefaultTempDir(ErasedOnReboot);
  Result.append(RequestedDir, RequestedDir + strlen(RequestedDir));
}

} // end namespace path

} // end namespace sys
} // end namespace llvm
