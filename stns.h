#ifndef STNS_H
#define STNS_H
#include <curl/curl.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "toml.h"
#include "parson.h"
#include <syslog.h>
#include <nss.h>
#include <grp.h>
#include <pwd.h>
#include <shadow.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <regex.h>
#define STNS_VERSION "2.0.0"
#define STNS_VERSION_WITH_NAME "stns/" STNS_VERSION
// 10MB
#define STNS_MAX_BUFFER_SIZE (10 * 1024 * 1024)
#define STNS_CONFIG_FILE "/etc/stns/client/stns.conf"
#define MAXBUF 1024
#define STNS_LOCK_FILE "/var/tmp/.stns.lock"
#define STNS_HTTP_NOTFOUND 404L
#define STNS_LOCK_RETRY 3
#define STNS_LOCK_INTERVAL_MSEC 10

typedef struct stns_response_t stns_response_t;
struct stns_response_t {
  char *data;
  size_t size;
  long status_code;
};

typedef struct stns_user_httpheader_t stns_user_httpheader_t;
struct stns_user_httpheader_t {
  char *key;
  char *value;
};
typedef struct stns_user_httpheaders_t stns_user_httpheaders_t;
struct stns_user_httpheaders_t {
  stns_user_httpheader_t *headers;
  size_t size;
};

typedef struct stns_conf_t stns_conf_t;
struct stns_conf_t {
  char *api_endpoint;
  char *auth_token;
  char *user;
  char *password;
  char *query_wrapper;
  char *chain_ssh_wrapper;
  char *http_proxy;
  int http_location;
  char *cache_dir;
  char *tls_cert;
  char *tls_key;
  char *tls_ca;
  int cached_enable;
  char *cached_unix_socket;
  stns_user_httpheaders_t *http_headers;
  int uid_shift;
  int gid_shift;
  int ssl_verify;
  int use_cached;
  int request_timeout;
  int request_retry;
  int request_locktime;
  int cache;
  int cache_ttl;
  int negative_cache_ttl;
};

extern int stns_load_config(char *, stns_conf_t *);
extern void stns_unload_config(stns_conf_t *);
extern int stns_request(stns_conf_t *, char *, stns_response_t *);
extern int stns_request_available(char *, stns_conf_t *);
extern void stns_make_lockfile(char *);
extern int stns_exec_cmd(char *, char *, stns_response_t *);
extern int stns_user_highest_query_available(int);
extern int stns_user_lowest_query_available(int);
extern int stns_group_highest_query_available(int);
extern int stns_group_lowest_query_available(int);
extern int pthread_mutex_retrylock(pthread_mutex_t *mutex);
extern void set_user_highest_id(int);
extern void set_user_lowest_id(int);
extern void set_group_highest_id(int);
extern void set_group_lowest_id(int);

#define STNS_ENSURE_BY(method_key, key_type, key_name, json_type, json_key, match_method, resource, ltype)             \
  enum nss_status ensure_##resource##_by_##method_key(char *data, stns_conf_t *c, key_type key_name,                   \
                                                      struct resource *rbuf, char *buf, size_t buflen, int *errnop)    \
  {                                                                                                                    \
    int i;                                                                                                             \
    JSON_Object *leaf;                                                                                                 \
    JSON_Value *root = json_parse_string(data);                                                                        \
                                                                                                                       \
    if (root == NULL) {                                                                                                \
      syslog(LOG_ERR, "%s(stns)[L%d] json parse error", __func__, __LINE__);                                           \
      return NSS_STATUS_UNAVAIL;                                                                                       \
    }                                                                                                                  \
    JSON_Array *root_array = json_value_get_array(root);                                                               \
    for (i = 0; i < json_array_get_count(root_array); i++) {                                                           \
      leaf = json_array_get_object(root_array, i);                                                                     \
      if (leaf == NULL) {                                                                                              \
        continue;                                                                                                      \
      }                                                                                                                \
      key_type current = json_object_get_##json_type(leaf, #json_key);                                                 \
                                                                                                                       \
      if (match_method) {                                                                                              \
        ltype##_ENSURE(leaf);                                                                                          \
        json_value_free(root);                                                                                         \
        return NSS_STATUS_SUCCESS;                                                                                     \
      }                                                                                                                \
    }                                                                                                                  \
    json_value_free(root);                                                                                             \
                                                                                                                       \
    return NSS_STATUS_NOTFOUND;                                                                                        \
  }

#define STNS_SET_DEFAULT_VALUE(buf, name, def)                                                                         \
  char buf[MAXBUF];                                                                                                    \
  if (name != NULL && strlen(name) > 0) {                                                                              \
    strcpy(buf, name);                                                                                                 \
  } else {                                                                                                             \
    strcpy(buf, def);                                                                                                  \
  }                                                                                                                    \
  name = buf;

#define STNS_GET_SINGLE_VALUE_METHOD(method, first, format, value, resource, query_available, id_shift)                \
  enum nss_status _nss_stns_##method(first, struct resource *rbuf, char *buf, size_t buflen, int *errnop)              \
  {                                                                                                                    \
    int curl_result;                                                                                                   \
    stns_response_t r;                                                                                                 \
    stns_conf_t c;                                                                                                     \
    char url[MAXBUF];                                                                                                  \
                                                                                                                       \
    if (stns_load_config(STNS_CONFIG_FILE, &c) != 0)                                                                   \
      return NSS_STATUS_UNAVAIL;                                                                                       \
    query_available;                                                                                                   \
    snprintf(url, sizeof(url), format, value id_shift);                                                                \
    curl_result = stns_request(&c, url, &r);                                                                           \
                                                                                                                       \
    if (curl_result != CURLE_OK) {                                                                                     \
      free(r.data);                                                                                                    \
      stns_unload_config(&c);                                                                                          \
      if (r.status_code == STNS_HTTP_NOTFOUND) {                                                                       \
        return NSS_STATUS_NOTFOUND;                                                                                    \
      }                                                                                                                \
      return NSS_STATUS_UNAVAIL;                                                                                       \
    }                                                                                                                  \
                                                                                                                       \
    int result = ensure_##resource##_by_##value(r.data, &c, value, rbuf, buf, buflen, errnop);                         \
    free(r.data);                                                                                                      \
    stns_unload_config(&c);                                                                                            \
    return result;                                                                                                     \
  }

#define SET_ATTRBUTE(type, name, attr)                                                                                 \
  int name##_length = strlen(name) + 1;                                                                                \
                                                                                                                       \
  if (buflen < name##_length) {                                                                                        \
    *errnop = ERANGE;                                                                                                  \
    return NSS_STATUS_TRYAGAIN;                                                                                        \
  }                                                                                                                    \
                                                                                                                       \
  strcpy(buf, name);                                                                                                   \
  rbuf->type##_##attr = buf;                                                                                           \
  buf += name##_length;                                                                                                \
  buflen -= name##_length;

#define STNS_SET_ENTRIES(type, ltype, resource, query)                                                                 \
  enum nss_status inner_nss_stns_set##type##ent(char *data, stns_conf_t *c)                                            \
  {                                                                                                                    \
    if (pthread_mutex_retrylock(&type##ent_mutex) != 0)                                                                \
      return NSS_STATUS_UNAVAIL;                                                                                       \
                                                                                                                       \
    entries          = NULL;                                                                                           \
    entry_idx        = 0;                                                                                              \
    JSON_Value *root = json_parse_string(data);                                                                        \
    if (root == NULL) {                                                                                                \
      pthread_mutex_unlock(&type##ent_mutex);                                                                          \
      syslog(LOG_ERR, "%s(stns)[L%d] json parse error", __func__, __LINE__);                                           \
      return NSS_STATUS_UNAVAIL;                                                                                       \
    }                                                                                                                  \
                                                                                                                       \
    entries = root;                                                                                                    \
                                                                                                                       \
    pthread_mutex_unlock(&type##ent_mutex);                                                                            \
    return NSS_STATUS_SUCCESS;                                                                                         \
  }                                                                                                                    \
                                                                                                                       \
  enum nss_status _nss_stns_set##type##ent(void)                                                                       \
  {                                                                                                                    \
    int curl_result;                                                                                                   \
    stns_response_t r;                                                                                                 \
    stns_conf_t c;                                                                                                     \
    if (stns_load_config(STNS_CONFIG_FILE, &c) != 0)                                                                   \
      return NSS_STATUS_UNAVAIL;                                                                                       \
                                                                                                                       \
    curl_result = stns_request(&c, #query, &r);                                                                        \
    if (curl_result != CURLE_OK) {                                                                                     \
      free(r.data);                                                                                                    \
      stns_unload_config(&c);                                                                                          \
      if (r.status_code == STNS_HTTP_NOTFOUND) {                                                                       \
        return NSS_STATUS_NOTFOUND;                                                                                    \
      }                                                                                                                \
      return NSS_STATUS_UNAVAIL;                                                                                       \
    }                                                                                                                  \
                                                                                                                       \
    int result = inner_nss_stns_set##type##ent(r.data, &c);                                                            \
    free(r.data);                                                                                                      \
    stns_unload_config(&c);                                                                                            \
    return result;                                                                                                     \
  }                                                                                                                    \
                                                                                                                       \
  enum nss_status _nss_stns_end##type##ent(void)                                                                       \
  {                                                                                                                    \
    if (pthread_mutex_retrylock(&type##ent_mutex) != 0)                                                                \
      return NSS_STATUS_UNAVAIL;                                                                                       \
    entry_idx = 0;                                                                                                     \
    if (entry_idx != 0)                                                                                                \
      json_value_free(entries);                                                                                        \
    entries = NULL;                                                                                                    \
    pthread_mutex_unlock(&type##ent_mutex);                                                                            \
    return NSS_STATUS_SUCCESS;                                                                                         \
  }                                                                                                                    \
                                                                                                                       \
  enum nss_status inner_nss_stns_get##type##ent_r(stns_conf_t *c, struct resource *rbuf, char *buf, size_t buflen,     \
                                                  int *errnop)                                                         \
  {                                                                                                                    \
    enum nss_status ret       = NSS_STATUS_SUCCESS;                                                                    \
    JSON_Array *array_entries = json_value_get_array(entries);                                                         \
                                                                                                                       \
    if (array_entries == NULL) {                                                                                       \
      ret = _nss_stns_set##type##ent();                                                                                \
    }                                                                                                                  \
                                                                                                                       \
    if (ret != NSS_STATUS_SUCCESS) {                                                                                   \
      return ret;                                                                                                      \
    }                                                                                                                  \
                                                                                                                       \
    if (entry_idx >= json_array_get_count(array_entries)) {                                                            \
      *errnop = ENOENT;                                                                                                \
      return NSS_STATUS_NOTFOUND;                                                                                      \
    }                                                                                                                  \
                                                                                                                       \
    JSON_Object *user = json_array_get_object(array_entries, entry_idx);                                               \
                                                                                                                       \
    ltype##_ENSURE(user);                                                                                              \
    entry_idx++;                                                                                                       \
    return NSS_STATUS_SUCCESS;                                                                                         \
  }                                                                                                                    \
                                                                                                                       \
  enum nss_status _nss_stns_get##type##ent_r(struct resource *rbuf, char *buf, size_t buflen, int *errnop)             \
  {                                                                                                                    \
    stns_conf_t c;                                                                                                     \
    if (stns_load_config(STNS_CONFIG_FILE, &c) != 0)                                                                   \
      return NSS_STATUS_UNAVAIL;                                                                                       \
    if (pthread_mutex_retrylock(&type##ent_mutex) != 0) {                                                              \
      stns_unload_config(&c);                                                                                          \
      return NSS_STATUS_UNAVAIL;                                                                                       \
    }                                                                                                                  \
    int result = inner_nss_stns_get##type##ent_r(&c, rbuf, buf, buflen, errnop);                                       \
    pthread_mutex_unlock(&type##ent_mutex);                                                                            \
    stns_unload_config(&c);                                                                                            \
    return result;                                                                                                     \
  }

#define SET_GET_HIGH_LOW_ID(highest_or_lowest, user_or_group)                                                          \
  void set_##user_or_group##_##highest_or_lowest##_id(int id)                                                          \
  {                                                                                                                    \
    if (pthread_mutex_retrylock(&user_or_group##_mutex) != 0)                                                          \
      return;                                                                                                          \
    highest_or_lowest##_##user_or_group##_id = id;                                                                     \
    pthread_mutex_unlock(&user_or_group##_mutex);                                                                      \
  }                                                                                                                    \
  int get_##user_or_group##_##highest_or_lowest##_id(void)                                                             \
  {                                                                                                                    \
    int r;                                                                                                             \
    if (pthread_mutex_retrylock(&user_or_group##_mutex) != 0)                                                          \
      return 0;                                                                                                        \
    r = highest_or_lowest##_##user_or_group##_id;                                                                      \
    pthread_mutex_unlock(&user_or_group##_mutex);                                                                      \
    return r;                                                                                                          \
  }

#define TOML_STR(m, empty)                                                                                             \
  c->m = malloc(strlen(empty) + 1);                                                                                    \
  strcpy(c->m, empty);
#define TOML_NULL_OR_INT(m, empty) c->m = empty;

#define GET_TOML_BYKEY(m, method, empty, str_or_int)                                                                   \
  if (0 != (raw = toml_raw_in(tab, #m))) {                                                                             \
    if (0 != method(raw, &c->m)) {                                                                                     \
      syslog(LOG_ERR, "%s(stns)[L%d] cannot parse toml file:%s key:%s", __func__, __LINE__, filename, #m);             \
    }                                                                                                                  \
  } else {                                                                                                             \
    str_or_int(m, empty)                                                                                               \
  }
#define GET_TOML_BY_TABLE_KEY(t, m, method, empty, str_or_int)                                                         \
  if (0 != (in_tab = toml_table_in(tab, #t))) {                                                                        \
    if (0 != (raw = toml_raw_in(in_tab, #m))) {                                                                        \
      if (0 != method(raw, &c->t##_##m)) {                                                                             \
        syslog(LOG_ERR, "%s(stns)[L%d] cannot parse toml file:%s key:%s", __func__, __LINE__, filename, #m);           \
      }                                                                                                                \
    } else {                                                                                                           \
      str_or_int(t##_##m, empty)                                                                                       \
    }                                                                                                                  \
  } else {                                                                                                             \
    str_or_int(t##_##m, empty)                                                                                         \
  }

#define UNLOAD_TOML_BYKEY(m)                                                                                           \
  if (c->m != NULL) {                                                                                                  \
    free(c->m);                                                                                                        \
  }

#define ID_QUERY_AVAILABLE(user_or_group, high_or_low, inequality)                                                     \
  int stns_##user_or_group##_##high_or_low##est_query_available(int id)                                                \
  {                                                                                                                    \
    int r = get_##user_or_group##_##high_or_low##est_id();                                                             \
    if (r != 0 && r inequality id)                                                                                     \
      return 0;                                                                                                        \
    return 1;                                                                                                          \
  }

#define USER_ID_QUERY_AVAILABLE                                                                                        \
  if (!stns_user_highest_query_available(uid) || !stns_user_lowest_query_available(uid))                               \
    return NSS_STATUS_NOTFOUND;

#define GROUP_ID_QUERY_AVAILABLE                                                                                       \
  if (!stns_group_highest_query_available(gid) || !stns_group_lowest_query_available(gid))                             \
    return NSS_STATUS_NOTFOUND;

#endif /* STNS_H */
