#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef WIN32
// These utilities are POSIX specific.
// They may work in MinGW but that needs testing.
// So currently they are disabled in windows environment.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <glibmm.h>

#include "FileUtils.h"
#include "User.h"

namespace Arc {

Glib::Mutex suid_lock;

static bool write_all(int h,const void* buf,size_t l) {
  for(;l>0;) {
    ssize_t ll = write(h,buf,l);
    if(ll == -1) return false;
    buf = (const void*)(((const char*)buf)+ll);
    l-=ll;
  }
  return true;
}

int FileOpen(const char* path,int flags,mode_t mode) {
  return FileOpen(path,flags,0,0,mode);
}

int FileOpen(const char* path,int flags,uid_t uid,gid_t gid,mode_t mode) {
  int h = -1;
  {
    UserSwitch usw(uid,gid);
    if(!usw) return -1;
    h = open(path,flags | O_NONBLOCK,mode);
  };
  if(h == -1) return -1;
  if(flags & O_NONBLOCK) return h;
  while(1) {
    pollfd ph;
    ph.fd=h; ph.events=POLLOUT; ph.revents=0;
    if(poll(&ph,1,-1) <= 0) {
      if(errno != EINTR) {
        close(h);
        return -1;
      };
    };
    if(ph.revents & POLLOUT) break;
  };
  int fl = fcntl(h,F_GETFL);
  if(fl == -1) {
    close(h);
    return -1;
  };
  fl &= ~O_NONBLOCK;
  if(fcntl(h,F_SETFL,fl) == -1) {
    close(h);
    return -1;
  };
  return h;
}

bool FileCopy(const char* source_path,const char* destination_path) {
  struct stat st;
  int source_handle = FileOpen(source_path,O_RDONLY,0,0,0);
  if(source_handle == -1) return false;
  if(!FileStat(source_path,&st,true)) return false;
  int destination_handle = FileOpen(destination_path,O_WRONLY | O_CREAT | O_TRUNC,0,0,st.st_mode);
  if(destination_handle == -1) {
    ::close(source_handle);
    return false;
  }
  bool r = FileCopy(source_handle,destination_handle);
  ::close(source_handle);
  ::close(destination_handle);
  return r;
}

bool FileCopy(const char* source_path,int destination_handle) {
  int source_handle = FileOpen(source_path,O_RDONLY,0,0,0);
  if(source_handle == -1) return false;
  if(::ftruncate(destination_handle,0) != 0) {
    ::close(source_handle);
    return false;
  }
  bool r = FileCopy(source_handle,destination_handle);
  ::close(source_handle);
  return r;
}

bool FileCopy(int source_handle,const char* destination_path) {
  int destination_handle = FileOpen(destination_path,O_WRONLY | O_CREAT | O_TRUNC,0,0,0600);
  if(destination_handle == -1) return false;
  bool r = FileCopy(source_handle,destination_handle);
  ::close(destination_handle);
  return r;
}

#define FileCopyBigThreshold (50*1024*1024)
#define FileCopyBufSize (4*1024)

bool FileCopy(int source_handle,int destination_handle) {
  size_t source_size = lseek(source_handle,0,SEEK_END);
  if( source_size == (off_t)(-1)) return false;
  if(source_size == 0) return true;
  if(source_size <= FileCopyBigThreshold) {
    void* source_addr = mmap(NULL,source_size,PROT_READ,MAP_SHARED,source_handle,0);
    if(source_addr != (void *)(-1)) {
      bool r = write_all(destination_handle,source_addr,source_size);
      munmap(source_addr,source_size);
      return r;
    }
  }
  if(lseek(source_handle,0,SEEK_SET) != 0) return false;
  char* buf = new char[FileCopyBufSize];
  if(!buf) return false;
  bool r = true;
  for(;;) {
    ssize_t l = FileCopyBufSize;
    l=::read(source_handle,buf,l);
    if(l == 0) break; // less than expected
    if(l == -1) {
      // EWOULDBLOCK
      r = false;
      break;
    }
    if(!write_all(destination_handle,buf,l)) {
      r = false;
      break;
    }
  }
  return r;
}

Glib::Dir* DirOpen(const char* path) {
  return DirOpen(path,0,0);
}

// TODO: find non-blocking way to open directory
Glib::Dir* DirOpen(const char* path,uid_t uid,gid_t gid) {
  Glib::Dir* dir = NULL;
  {
    UserSwitch usw(uid,gid);
    if(!usw) return NULL;
    try {
      dir = new Glib::Dir(path);
    } catch(Glib::FileError& e) {
      // err=e.what();
    };
  };
  return dir;
}

bool FileStat(const char* path,struct stat *st,bool follow_symlinks) {
  return FileStat(path,st,0,0,follow_symlinks);
}

// TODO: maybe by using open + fstat it would be possible to 
// make this functin less blocking
bool FileStat(const char* path,struct stat *st,uid_t uid,gid_t gid,bool follow_symlinks) {
  int r = -1;
  {
    UserSwitch usw(uid,gid);
    if(!usw) return false;
    if(follow_symlinks) {
      r = ::stat(path,st);
    } else {
      r = ::lstat(path,st);
    };
  };
  return (r == 0);
}

bool DirCreate(const char* path,mode_t mode,bool with_parents) {
  return DirCreate(path,0,0,mode,with_parents);
}

// TODO: find non-blocking way to create directory
bool DirCreate(const char* path,uid_t uid,gid_t gid,mode_t mode,bool with_parents) {
  int r = -1;
  {
    UserSwitch usw(uid,gid);
    if(!usw) return false;
    if(::mkdir(path,mode) == 0) return true;
  }
  if(errno == EEXIST) {
    /*
    Should it be just dumb mkdir or something clever?
    struct stat st; 
    r = ::stat(path,&st);
    if((r == 0) && (S_ISDIR(st.st_mode))) {
      if((uid == 0) || (st.st_uid == uid)) {
        if((gid == 0) || (st.st_gid == gid) ||
           (::chown(path,(uid_t)(-1),gid) == 0)) {
          // mode ?
          return true;
        }
      }
    }
    */
    return true;
  } else if(errno == ENOENT) {
    if(with_parents) {
      std::string ppath(path);
      if(!Glib::path_is_absolute(ppath)) {
        ppath=Glib::get_current_dir()+G_DIR_SEPARATOR_S+ppath;
      }
      std::string::size_type pos = ppath.rfind(G_DIR_SEPARATOR_S);
      if((pos != 0) && (pos != std::string::npos)) {
        ppath.resize(pos);
        if(!DirCreate(ppath.c_str(),uid,gid,mode,true)) return false;
        UserSwitch usw(uid,gid);
        if(!usw) return false;
        if(::mkdir(path,mode) == 0) return true;
      }
    }
  }
  return false;
}


bool DirDelete(const char* path,uid_t uid,gid_t gid) {

  bool r = false;
  {
    UserSwitch usw(uid, gid);
    if (!usw) return false;
    r = DirDelete(path);
  }
  return r;
}  

bool DirDelete(const char* path) {

  struct stat st;
  if (::stat(path, &st) != 0 || ! S_ISDIR(st.st_mode))
    return false;
  try {
    Glib::Dir dir(path);
    std::string file_name;
    while ((file_name = dir.read_name()) != "") {
      std::string fullpath(path);
      fullpath += '/' + file_name;
      if (::lstat(fullpath.c_str(), &st) != 0) return false;
      if (S_ISDIR(st.st_mode)) {
        if (!DirDelete(fullpath.c_str())) {
          return false;
        }
      } else {
        if (::remove(fullpath.c_str()) != 0) {
          return false;
        }
      }
    } 
  }
  catch (Glib::FileError& e) {
    return false;
  }
  if (rmdir(path) != 0) return false;
      
  return true;
}

} // namespace Arc

#endif

