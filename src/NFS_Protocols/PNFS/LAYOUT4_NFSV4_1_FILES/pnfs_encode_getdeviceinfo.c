/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    pnfs_encode_getdeviceinfo.c
 * \brief   encode the addr_body_val structure in GETDEVICEINFO
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include <string.h>
#include <signal.h>

#include "stuff_alloc.h"

#ifdef _USE_GSSRPC
#include <gssrpc/rpc.h>
#else
#include <rpc/rpc.h>
#endif

#include "PNFS/LAYOUT4_NFSV4_1_FILES/pnfs_layout4_nfsv4_1_files.h"
#include "nfs_core.h"

void local_print_buff(char *buff, int len)
{
  int i = 0;

  printf("  Len=%u|%x Buff=%p Val: ", len, len, buff);
  for(i = 0; i < len; i++)
    printf("%02X ", buff[i]);
  printf("\n");
}                             

/**
 *
 * pnfs_encode_getdeviceinfo: encodes the addr_body_val structure in GETDEVICEINFO.
 *
 * Encode the addr_body_val structure in GETDEVICEINFO.
 *
 * @param pnfsclient        [INOUT] pointer to the pnfsclient structure (client to the ds).
 * @param pnfs_layout_param [IN]    pointer to pnfs layoutfile configuration
 *
 * @return  nothing (void function)
 * @return -1 if one of its argument is NULL, exists if failed
 *
 */

extern nfs_parameter_t nfs_param;

void pnfs_encode_getdeviceinfo( char * buff, unsigned int * plen )
{
  unsigned int offset = 0 ;
  uint32_t int32 = 0 ;
  char tmpchar[MAXNAMLEN] ;
  unsigned int tmplen = 0 ;
  unsigned int padlen = 0 ;
 
  /* nflda_stripe_indices.nflda_stripe_indices_len */
  int32 = htonl( 1 ) ;
  memcpy( (char *)(buff+offset), (char *)&int32, sizeof( int32 ) );
  offset +=  sizeof( int32 ) ;


  /* nflda_stripe_indices.nflda_stripe_indices_val */
  int32 = 0 ; 
  memcpy( (char *)(buff+offset), (char *)&int32, sizeof( int32 ) );
  offset +=  sizeof( int32 ) ;

  /* nflda_multipath_ds_list.nflda_multipath_ds_list_len */
  int32 = htonl( 1 ) ;
  memcpy( (char *)(buff+offset), (char *)&int32, sizeof( int32 ) );
  offset +=  sizeof( int32 ) ;

  /* nflda_multipath_ds_list.nflda_multipath_ds_list_val[0].multipath_list4_len */
  int32 = htonl( 1 ) ;
  memcpy( (char *)(buff+offset), (char *)&int32, sizeof( int32 ) );
  offset +=  sizeof( int32 ) ;

   /* nflda_multipath_ds_list.nflda_multipath_ds_list_val[0].multipath_list4_val[0].na_r_netid */
   int32 = htonl( 3 ) ; /* because strlen( "tcp" ) = 3 */
   memcpy( (char *)(buff+offset), (char *)&int32, sizeof( int32 ) );
   offset +=  sizeof( int32 ) ;

   memset( tmpchar, 0, MAXNAMLEN ) ;
   strncpy( tmpchar, "tcp", MAXNAMLEN );
   memcpy( (char *)(buff+offset), tmpchar, 4 ) ; /* 4 bytes = 3 bytes for "tcp" and 1 to keep XDR alignment */
   offset += 4 ;
   
   /* nflda_multipath_ds_list.nflda_multipath_ds_list_val[0].multipath_list4_val[0].na_r_addr */
   memset( tmpchar, 0, MAXNAMLEN ) ;
   snprintf( tmpchar, MAXNAMLEN, "%s.%u.%u",
	     nfs_param.pnfs_param.layoutfile.ds_param[0].ipaddr_ascii,
             nfs_param.pnfs_param.layoutfile.ds_param[0].ipport & 0x0F, 
             nfs_param.pnfs_param.layoutfile.ds_param[0].ipport >> 8) ;
   tmplen = strnlen( tmpchar, MAXNAMLEN ) ;

   /* XDR padding : keep stuff aligned on 32 bits pattern */
   if( tmplen % 4 == 0)
     padlen = 0;
   else
     padlen = 4 - ( tmplen % 4 ) ;

   /* len of na_r_addr */
   int32 = htonl( tmplen ) ; 
   memcpy( (char *)(buff+offset), (char *)&int32, sizeof( int32 ) );
   offset +=  sizeof( int32 ) ;

   memcpy( (char *)(buff+offset), tmpchar, tmplen+padlen );
   offset += tmplen + padlen ;

   *plen = offset ;

   local_print_buff( buff, offset ) ;
} /* pnfs_encode_getdeviceinfo */

