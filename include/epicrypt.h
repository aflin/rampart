#ifndef EPICRYPT_H
#define EPICRYPT_H
/**********************************************************************/
char *epi_encrypt  (char *key,char *buf,size_t *sz,int inplace);
char *epi_decrypt  (char *key,char *buf,size_t *sz,int inplace);
char *epi_encrypt_ecb  (char key[8],char *buf,size_t *sz,int inplace);
char *epi_decrypt_ecb  (char key[8],char *buf,size_t *sz,int inplace);
char *epi_crytoasc (char *buf,size_t *sz);
char *epi_asctocry (char *buf,size_t *sz);
/**********************************************************************/
#endif                                                  /* EPICRYPT_H */
