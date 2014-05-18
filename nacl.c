
#include <jim.h>
#include <string.h>

#include "tweetnacl.h"

#define rc(x) printf("Ref Count <%s>: %d\n",#x,x->refCount)

static Jim_Obj *Jim_EmptyString(Jim_Interp *interp, int length) {
    Jim_Obj *empty = Jim_NewObj(interp);
    empty->bytes = Jim_Alloc(length + 1);
    empty->length = length;
    empty->typePtr = NULL;
    memset(empty->bytes, 0, length + 1);
    return empty;
}

static Jim_Obj *Jim_HexString(Jim_Interp *interp, Jim_Obj *s) {
    int i;
    int len = Jim_Length(s);
    Jim_Obj *hex = Jim_EmptyString(interp,2 * len);

    for (i=0; i<len; i++) {
        sprintf(hex->bytes+(2*i),"%02x",s->bytes[i]);
    }

    return hex;
}

static int unhex(char c) {
    if (c >= '0' && c <= '9') {
        return (c - '0');
    } else if (c >= 'A' && c <= 'F') {
        return (c - 'A' + 10);
    } else if (c >= 'a' && c <= 'f') {
        return (c - 'a' + 10);
    } else {
        return -1;
    }
}

static Jim_Obj *Jim_Unhex(Jim_Interp *interp, Jim_Obj *hex) {
    int i,b1,b2;
    int err = 0;
    int len = Jim_Length(hex);
    const char *s = Jim_String(hex);

    if ((len % 2) != 0) {
        return NULL;
    }

    Jim_Obj *bin = Jim_EmptyString(interp,len/2);

    for (i=0; i<len/2; i++) {
        if ((b1 = unhex(s[2*i])) == -1 ||
            (b2 = unhex(s[2*i+1])) == -1) {
            ++err;
            break;
        } else {
            bin->bytes[i] = (unsigned char)((b1 * 16) + b2);
        }
    }

    if (err > 0) {
        Jim_FreeNewObj(interp,bin);
        return NULL;
    } else {
        return bin;
   }
}


static int Hexdump_Cmd(Jim_Interp *interp, int argc,
                                   Jim_Obj *const argv[]) {
    if (argc != 2) {
        Jim_WrongNumArgs(interp,1,argv,"<string>");
        return JIM_ERR;
    }

    Jim_SetResult(interp,Jim_HexString(interp,argv[1]));

    return JIM_OK;
}

static int Unhexdump_Cmd(Jim_Interp *interp, int argc,
                                   Jim_Obj *const argv[]) {
    if (argc != 2) {
        Jim_WrongNumArgs(interp,1,argv,"<string>");
        return JIM_ERR;
    }

    Jim_Obj *s = Jim_Unhex(interp,argv[1]);
    if (s == NULL) {
        Jim_SetResultString(interp,"Invalid hex string",-1);
        return JIM_ERR;
    } else {
        Jim_SetResult(interp,s);
        return JIM_OK;
    }
}

static int RandomBytes_Cmd(Jim_Interp *interp, int argc, 
                           Jim_Obj *const argv[]) {
    if (argc != 2) {
        Jim_WrongNumArgs(interp,1,argv,"<bytes>");
        return JIM_ERR;
    }

    long len;
    if (Jim_GetLong(interp, argv[1], &len) != JIM_OK) {
        Jim_SetResultString(interp,"Invalid length",-1);
        return JIM_ERR;
    }

    Jim_Obj *random = Jim_EmptyString(interp,len);

    randombytes(random->bytes,len);

    Jim_SetResult(interp,random);

    return JIM_OK;
}

static int SecretBoxOpen_Cmd(Jim_Interp *interp, int argc, Jim_Obj *const argv[]) {

    if (argc != 4) {
        Jim_WrongNumArgs(interp,1,argv,"<key> <nonce> <message>");
        return JIM_ERR;
    }

    Jim_Obj *key = argv[1];
    Jim_Obj *nonce = argv[2];
    Jim_Obj *cipher = argv[3];
    Jim_Obj *msg= Jim_EmptyString(interp,cipher->length);

    crypto_secretbox_open(msg->bytes,
                          cipher->bytes,
                          cipher->length,
                          nonce->bytes,
                          key->bytes);

    Jim_SetResult(interp,msg);

    //Jim_Free(msg);

    return JIM_OK;
}

static int SecretBox_Cmd(Jim_Interp *interp, int argc, Jim_Obj *const argv[]) {

    int hex = 0;

    if (argc > 1 && Jim_CompareStringImmediate(interp, argv[1], "-hex")) {
        hex = 1;
        --argc;
        ++argv;
    }

    if (argc != 3) {
        Jim_WrongNumArgs(interp,1,argv,"[-hex] <key> <message>");
        return JIM_ERR;
    }

    char buf[10];
    if (Jim_Length(argv[1]) != crypto_secretbox_KEYBYTES) {
        snprintf(buf,sizeof(buf),"%d",crypto_secretbox_KEYBYTES);
        Jim_SetResultFormatted(interp,
                               "Invalid key length [should be %s bytes]",buf);
        return JIM_ERR;
    }

    Jim_Obj *key = argv[1];
    Jim_Obj *msg = argv[2];
    int len = Jim_Length(msg);

    Jim_Obj *nonce = Jim_EmptyString(interp,crypto_secretbox_NONCEBYTES);
    Jim_Obj *msg_pad = Jim_EmptyString(interp,len + crypto_secretbox_ZEROBYTES);
    Jim_Obj *cipher = Jim_EmptyString(interp,len + crypto_secretbox_ZEROBYTES);

    randombytes(nonce->bytes,crypto_secretbox_NONCEBYTES);
    memcpy(msg_pad->bytes + crypto_secretbox_ZEROBYTES,msg->bytes,len);

    crypto_secretbox(cipher->bytes,
                     msg_pad->bytes,
                     msg_pad->length,
                     nonce->bytes,
                     key->bytes);
            
    Jim_Obj *result = Jim_NewListObj(interp,NULL,0);
    if (hex == 1) {
        Jim_ListAppendElement(interp,result,Jim_HexString(interp,nonce)); 
        Jim_ListAppendElement(interp,result,Jim_HexString(interp,cipher)); 
    } else {
        Jim_ListAppendElement(interp,result,nonce); 
        Jim_ListAppendElement(interp,result,cipher); 
    }

    rc(msg_pad);
    rc(nonce);
    rc(cipher);

    //Jim_Free(msg_pad);
    //Jim_Free(nonce);
    //Jim_Free(cipher);

    Jim_SetResult(interp,result);
    return JIM_OK;
}

Jim_naclInit(Jim_Interp *interp)
{
    Jim_CreateCommand(interp, "hexdump", Hexdump_Cmd, NULL, NULL);
    Jim_CreateCommand(interp, "unhexdump", Unhexdump_Cmd, NULL, NULL);
    Jim_CreateCommand(interp, "secretbox", SecretBox_Cmd, NULL, NULL);
    Jim_CreateCommand(interp, "secretbox_open", SecretBoxOpen_Cmd, NULL, NULL);
    Jim_CreateCommand(interp, "randombytes", RandomBytes_Cmd, NULL, NULL);
    return JIM_OK;
}

