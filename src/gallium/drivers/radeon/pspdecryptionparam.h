/*
***************************************************************************************************
* Copyright (c) 2015 Advanced Micro Devices, Inc. (unpublished)
*
*  All rights reserved.  This notice is intended as a precaution against inadvertent publication and
*  does not imply publication or any waiver of confidentiality.  The year included in the foregoing
*  notice is the year of creation of the work.
*
***************************************************************************************************
*/
/**
***************************************************************************************************
* @file  pspdecryptionparam.h
* @brief Decryption parameter definitions for VAAPI protected content decryption
***************************************************************************************************
*/
#ifndef _PSP_DECRYPTION_PARAM_H_
#define _PSP_DECRYPTION_PARAM_H_

typedef struct _DECRYPT_PARAMETERS_
{
   uint32_t                frame_size;         // Size of encrypted frame
   uint8_t                 encrypted_iv[16];   // IV of the encrypted frame (clear)
   uint8_t                 encrypted_key[16];  // key to decrypt encrypted frame (encrypted with session key)
   uint8_t                 session_iv[16];     // IV to be used to decrypt encrypted_key

   union
   {
      struct
      {
         uint32_t    drm_id   : 4;	//DRM session ID
         uint32_t    ctr      : 1;
         uint32_t    cbc      : 1;
         uint32_t    reserved : 26;
      } s;
      uint32_t        value;
   } u;
} DECRYPT_PARAMETERS;

#endif //_PSP_DECRYPTION_PARAM_H_
