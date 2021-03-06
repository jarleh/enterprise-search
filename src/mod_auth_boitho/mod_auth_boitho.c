/*
 * mod_auth_boitho.c
 *
 * An apache module to authenticate using the /etc/shadow file.
 * This module interacts with another program "validate", which
 * is setuid root.  Thus the /etc/shadow file can remain 
 * root:root 0400.
 *
 * Author: Brian Duggan <bduggan@matatu.org>
 * Some code was taken from the sample code supplied with
 * _Apache Modules_ by Stein and MacEachern.  Parts of this
 * were also influenced by mod_auth.c.
 *
 * Adapted for Apache2: Bernard du Breuil 
 *	<bernard.l.dubreuil@erdc.usace.army.mil>
 * I went back to mod_auth.c to see how it was converted.
 */

#define DEBUG
// runarb 06.04.2011:
// hardcoded. Skal egentlig v�re spesifisert i config/main.conf. N� blir det tul med andre utviklere...
#define CMDPORT 7392


/* The longest allowable length of a username */
#define MAX_USERNAME_LENGTH 100

/* The longest allowable length of the plaintext password*/
#define MAX_PW_LENGTH 100

/* How many seconds to sleep on a failed validation */
#define SLEEP_SECONDS (3)

/* Whether or not to record failed attempts in the system log defined=yes, not defined=no */
#define LOG_FAILED_ATTEMPTS

//if v1.3
//#define APACHE_V13
#ifdef APACHE_V13
	#define apr_pool_t pool
	#define apr_palloc ap_pcalloc
	//#define APR_OFFSETOF XtOffsetOf
#else
	#include "apr_strings.h"

#endif


#include "../crawlManager2/client.h"
#include "ap_config.h"
#include "httpd.h"  
#include "http_config.h"  
#include "http_core.h"  
#include "http_log.h"  
#include "http_protocol.h"  
#include "http_request.h"  

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pwd.h>
#include <grp.h>
#include <ctype.h>

#ifndef INSTBINDIR
#error INSTBINDIR should be defined as the location of the validate executable
<><><><><><>Crash and burn<><><><><>
#endif

#define PBUFSIZ (512)


/* do not accept empty "" strings */
#define strtrue(s) (s && *s)

/* 
 * configure like so:
 * 
 * LoadModule auth_boitho_module modules/mod_auth_boitho.so
 * <Location /test>
 * AuthType Basic 
 * AuthName WhateverAuthnameYouWant
 * AuthBoitho on
 * require valid-user 
 * </Location>
 */

typedef struct {
    int auth_boitho_flag; /* 1 for yes, 0 for no */
} auth_boitho_config_rec;

static void *create_auth_boitho_dir_config(apr_pool_t *p, char *d)
{
    auth_boitho_config_rec *sec =
    (auth_boitho_config_rec *) apr_palloc(p, sizeof(*sec));
    sec->auth_boitho_flag = 0;	
    return sec;
}

/*
 * Config file commands that this module can handle
 */
#ifdef APACHE_V13
static const command_rec auth_boitho_cmds[] =
{
	{"AuthBoitho", ap_set_flag_slot,
     		(void *) XtOffsetOf(auth_boitho_config_rec, auth_boitho_flag),
     OR_AUTHCFG, FLAG,
     "On or Off depending on whether to use mod_auth_boitho module"},
    {NULL}

};
#else
static const command_rec auth_boitho_cmds[] =
{
    AP_INIT_FLAG("AuthBoitho", ap_set_flag_slot,
	(void *)APR_OFFSETOF(auth_boitho_config_rec, auth_boitho_flag),
     OR_AUTHCFG, "On or Off depending on whether to use mod_auth_boitho module"),
    {NULL}
};
#endif

static const char module_name[] = "mod_auth_boitho";

static void my_register_hooks();

/* Internal functions */
static int auth_boitho_authorize(const char *user, const char* pw, 
		request_rec* r);

// Authentication function
static int auth_boitho_user_id(request_rec *r);

// Authorization function
static int auth_boitho_valid_user(request_rec *r);



#ifdef APACHE_V13
module MODULE_VAR_EXPORT auth_boitho_module =
{
    STANDARD_MODULE_STUFF,
    NULL,                       /* initializer */
    create_auth_boitho_dir_config,     /* dir config creater */
    NULL,                       /* dir merger --- default is to override */
    NULL,                       /* server config */
    NULL,                       /* merge server config */
    auth_boitho_cmds,                  /* command table */
    NULL,                       /* handlers */
    NULL,                       /* filename translation */
    auth_boitho_user_id,    /* check_user_id */
    auth_boitho_valid_user,          /* check auth */
    NULL,                       /* check access */
    NULL,                       /* type_checker */
    NULL,                       /* fixups */
    NULL,                       /* logger */
    NULL,                       /* header parser */
    NULL,                       /* child_init */
    NULL,                       /* child_exit */
    NULL                        /* post read-request */
};

#else
module AP_MODULE_DECLARE_DATA auth_boitho_module = 
{
    STANDARD20_MODULE_STUFF, 
    create_auth_boitho_dir_config , /* dir config creator */
    NULL,                  /* merge  per-dir    config structures */
    NULL,                  /* create per-server config structures */
    NULL,                  /* merge  per-server config structures */
    auth_boitho_cmds,      /* [config file] command table */
    my_register_hooks	   /* register hooks */
};

/* Apache 2 hooks for functions other than the handler itself. */
static void my_register_hooks()
{
	/* We want to run before mod_auth runs. */
	static const char *aszpost[] = {"mod_auth.c", NULL};
	/* Authentication function */
	ap_hook_check_user_id(auth_boitho_user_id,NULL,aszpost,APR_HOOK_MIDDLE);
	/* Authorization function */
	ap_hook_auth_checker(auth_boitho_valid_user, NULL, aszpost, APR_HOOK_MIDDLE);
}

#endif


static int auth_boitho_valid_user(request_rec *r)
{
       /* Any user will do. */
      return OK;
}

/*
 * auth_boitho_authorize
 *
 * See if a username/pw combination is valid.
 *
 * Returns 1 if the pw is correct, 0 if it's incorrect, -1 if there's an error.
 */

static int auth_boitho_authorize(const char *user, const char* pw, 
		request_rec* r)
{
    int status;
    int ret;
    int socketha;
    int errorbufflen = 512;
    char errorbuff[errorbufflen];


        if (!cmc_conect(&socketha,errorbuff,errorbufflen,CMDPORT)) {
        	printf("Error: %s\n",errorbuff);
        	return 0;
        }

	if ((ret = cmc_authuser(socketha, user, pw , 0 , "")) == 1) {
		status=1;
	}
	else if (ret == 2) {
		status = 2;
	}
	else {
		status=0;
	}

	cmc_close(socketha);

    if (status==1) {
	//runarb: 
	#ifdef DEBUG
		#ifdef APACHE_V13
			ap_log_error(APLOG_MARK, APLOG_ERR, r->server, "%s: User \"%s\" authenticated.",module_name,user);
		#else 
			ap_log_error(APLOG_MARK, APLOG_EMERG, 0, r->server, "%s: User \"%s\" authenticated.",module_name,user);
		#endif
	#endif

        return 1;  /* Correct pw */
    }
    else if (status == 2) {
	return 2; /* Not allowed */
    }
    else {
	return 0; /* Nope */
    }
}

int
ishexdigit(int c)
{
	if (isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))
		return 1;
	return 0;
}

#define MAX_STRING_FIX (MAX_USERNAME_LENGTH > MAX_PW_LENGTH ? MAX_USERNAME_LENGTH : MAX_PW_LENGTH)

/* converts username and passwords from latin1 to utf-8 */
void
cleanstring(char *str, void *r)
{
	int i, j;
	char s[MAX_STRING_FIX + 1];

	for (i = 0, j = 0; str[i] != '\0' && j < MAX_STRING_FIX-1; i++) {
		if (str[i] == '\xe6') { /* aelig */
			s[j++] = '\xc3';
			s[j++] = '\xa6';
		} else if (str[i] == '\xf8') { /* oslash */
			s[j++] = '\xc3';
			s[j++] = '\xb8';
		} else if (str[i] == '\xe5') { /* aring */
			s[j++] = '\xc3';
			s[j++] = '\xa5';
		} else if (str[i] == '\xc6') { /* AELIG */
			s[j++] = '\xc3';
			s[j++] = '\x86';
		} else if (str[i] == '\xd8') { /* OSLASH */
			s[j++] = '\xc3';
			s[j++] = '\x98';
		} else if (str[i] == '\xc5') { /* ARING */
			s[j++] = '\xc3';
			s[j++] = '\x85';
		} else {
			s[j++] = str[i];
		}
	}
	s[j] = '\0';
	strcpy(str, s);
}

/*
 * auth_boitho_user_id
 *
 * See if the username/pw combination is valid.
 */
static int auth_boitho_user_id(request_rec *r)
{

	/*
	#ifdef APACHE_V13
	ap_log_error(APLOG_MARK, APLOG_ERR, r->server,"test");
	#else
	ap_log_error(APLOG_MARK, APLOG_EMERG, 0, r->server, "test\n");
	#endif
	*/
     int ret;
     const char *sent_pw; 
     char str[200];
     int rc = ap_get_basic_auth_pw(r, &sent_pw); 
     char user[MAX_USERNAME_LENGTH+1];
     char passwd[MAX_PW_LENGTH+1];
     int n;
     auth_boitho_config_rec *s =
        (auth_boitho_config_rec *) ap_get_module_config(r->per_dir_config, &auth_boitho_module);           

     if(rc != OK) return rc;
     if (s->auth_boitho_flag != 1) { 	
		return DECLINED; 
     }            


#ifdef APACHE_V13
	conn_rec *c = r->connection;
     if(!(strtrue(c->user) && strtrue(sent_pw))) {
         ap_log_rerror(APLOG_MARK, APLOG_ERR, r,
                "Both a username and password must be provided for %s",
                   r->uri);
         ap_note_basic_auth_failure(r);
         return HTTP_UNAUTHORIZED;
     }

     /* Truncate to max length. */
     n = strlen(c->user);
     if (n > MAX_USERNAME_LENGTH) n=MAX_USERNAME_LENGTH;
     strncpy(user,c->user,n); /* Copy to user[0..n-1] */
     user[n] = '\0';

     n = strlen(sent_pw);
     if (n > MAX_PW_LENGTH) n=MAX_PW_LENGTH;
     strncpy(passwd,sent_pw,n); /* Copy to passwd[0..n-1] */
     passwd[n] = '\0';

#else
     if(!(strtrue(r->user) && strtrue(sent_pw))) {
         ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
		"Both a username and password must be provided for %s", 
                   r->uri);  
         ap_note_basic_auth_failure(r);  
         return HTTP_UNAUTHORIZED;
     }

     /* Truncate to max length. */
     n = strlen(r->user);
     if (n > MAX_USERNAME_LENGTH) n=MAX_USERNAME_LENGTH;
     strncpy(user,r->user,n); /* Copy to user[0..n-1] */
     user[n] = '\0';

     n = strlen(sent_pw);
     if (n > MAX_PW_LENGTH) n=MAX_PW_LENGTH;
     strncpy(passwd,sent_pw,n); /* Copy to passwd[0..n-1] */
     passwd[n] = '\0';
#endif

     cleanstring(user, r);
     cleanstring(passwd, r);

     ret = auth_boitho_authorize(user,passwd,r);
     if (ret==-1)
         return HTTP_INTERNAL_SERVER_ERROR;
     if (ret == 0) {
	#ifdef APACHE_V13
		ap_log_rerror(APLOG_MARK, APLOG_ERR, r, "Invalid password entered for user %s", user);
	#else
         	ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "Invalid password entered for user %s", user);  
	#endif
         ap_note_basic_auth_failure(r);  
         return HTTP_UNAUTHORIZED;
     } else if (ret == 2) {
	#ifdef APACHE_V13
		return HTTP_INTERNAL_SERVER_ERROR;
	#else
        	apr_table_setn(r->headers_out, "Location", "/noaccess.html");
		return HTTP_MOVED_TEMPORARILY;
	#endif
     }


     return OK;
}

