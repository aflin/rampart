#ifndef __PWD_H__
#define __PWD_H__
#ifdef __cplusplus
extern "C" {
#endif

struct passwd {
        char    *pw_name;
        char    *pw_passwd;
        unsigned short  pw_upad;        /* pads added to allow POSIX compliancy for */
        unsigned short  pw_uid;         /* uid_t and gid_t types. */
        unsigned short  pw_gpad;
        unsigned short  pw_gid;
};

#ifdef __cplusplus
}
#endif
#endif /* !__PWD_H__ */
