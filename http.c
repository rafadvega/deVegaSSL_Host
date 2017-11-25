
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
//#include "base64.h"

#define HTTP_PORT        80
#define BUFFER_SIZE     255
#define MAX_GET_COMMAND 255

/**
 * Parse a well-formed url and return pointers to the host and the path
 * 
 * @param uri pointer to the complete url
 * @param host pointer to the host part
 * @param path pointer to de path part
 * @return -1 if te url is invalid
 */
int parse_url(char *uri, char **host, char **path) {
    char *pos;

    pos = strstr(uri, "//");

    if (!pos) {
        return -1;
    }

    *host = pos + 2;
    pos = strchr(*host, '/');

    if (!pos) {
        *path = NULL;
    } else {
        *pos = '\0';
        *path = pos + 1;
    }

    return 0;
}

void display_result(int connection) {
    int received = 0;
    static char recv_buf[ BUFFER_SIZE + 1 ];

    while ((received = recv(connection, recv_buf, BUFFER_SIZE, 0)) > 0) {
        recv_buf[ received ] = '\0';
        printf("%s", recv_buf);
    }
    printf("\n");
}

int http_get(int connection, const char *path, const char *host,
        const char *proxy_host,
        const char *proxy_user,
        const char *proxy_password) {
    static char get_command[ MAX_GET_COMMAND ];

    if (proxy_host) {
        sprintf(get_command, "GET http://%s/%s HTTP/1.1\r\n", host, path);
    } else {
        sprintf(get_command, "GET /%s HTTP/1.1\r\n", path);
    }
    if (send(connection, get_command, strlen(get_command), 0) == -1) {
        return -1;
    }

    sprintf(get_command, "Host: %s\r\n", host);
    if (send(connection, get_command, strlen(get_command), 0) == -1) {
        return -1;
    }

    if (proxy_user) {
        int credentials_len = strlen(proxy_user) + strlen(proxy_password) + 1;
        char *proxy_credentials = malloc(credentials_len);
        char *auth_string = malloc(((credentials_len * 4) / 3) + 1);
        sprintf(proxy_credentials, "%s:%s", proxy_user, proxy_password);
        //base64_encode( proxy_credentials, credentials_len, auth_string );
        sprintf(get_command, "Proxy-Authorization: BASIC %s\r\n", auth_string);
        if (send(connection, get_command, strlen(get_command), 0) == -1) {
            free(proxy_credentials);
            free(auth_string);
            return -1;
        }
        free(proxy_credentials);
        free(auth_string);
    }

    sprintf(get_command, "Connection: close\r\n\r\n");
    if (send(connection, get_command, strlen(get_command), 0) == -1) {
        return -1;
    }

    return 0;
}

int parse_proxy_param( char *proxy_spec,
            char **proxy_host,
            int *proxy_port,
            char **proxy_user,
            char **proxy_password )
{
  char *login_sep, *colon_sep, *trailer_sep;
  // Technically, the user should start the proxy spec with
  // "http://". But, be forgiving if he didn't.
  if ( !strncmp( "http://", proxy_spec, 7 ) )
  {
   proxy_spec += 7;
  } 
  login_sep = strchr( proxy_spec, '@' );
 
  if ( login_sep )
  {
    colon_sep = strchr( proxy_spec, ':' );
    if ( !colon_sep || ( colon_sep > login_sep ) )
    {
      // Error - if username supplied, password must be supplied.
      fprintf( stderr, "Expected password in '%s'\n", proxy_spec );
      return 0;
    }
    *colon_sep = '\0';
    *proxy_user = proxy_spec;
    *login_sep = '\0';
    *proxy_password = colon_sep + 1;
    proxy_spec = login_sep + 1;
  }
  // If the user added a "/" on the end (as they sometimes do),
  // just ignore it.
  trailer_sep = strchr( proxy_spec, '/' );
  if ( trailer_sep )
  {
    *trailer_sep = '\0';
  }

  colon_sep = strchr( proxy_spec, ':' );
  if ( colon_sep )
  {
    // non-standard proxy port
    *colon_sep = '\0';
    *proxy_host = proxy_spec;
    *proxy_port = atoi( colon_sep + 1 );
    if ( *proxy_port == 0 )
    {
     return 0;
    }
  }
  else
  { 
    *proxy_port = HTTP_PORT;
    *proxy_host = proxy_spec;
  }
  return 1;
}

/*
 * HTTP client
 */
int main(int argc, char *argv[]) {

    int client_connection;
    struct hostent *host_name;
    struct sockaddr_in host_address;
    char *proxy_host = NULL;
    char *proxy_user, *proxy_password;
    int proxy_port;
    char *host;
    char *path;
    int ind = 1;

    if (argc < 2) {
        fprintf(stderr,
                "Usage: %s: [-p http://[username:password@]proxy-host:proxy-port] <URL>\n",
                argv[ 0 ]);
        return 1;
    }
    proxy_host = proxy_user = proxy_password = host = path = NULL;
    ind = 1;
    if (!strcmp("-p", argv[ ind ])) {
        if (!parse_proxy_param(argv[ ++ind ], &proxy_host, &proxy_port,
                &proxy_user, &proxy_password)) {
            fprintf(stderr, "Error - malformed proxy parameter '%s'.\n", argv[ 2 ]);
            return 2;
        }
        ind++;
    }

    if (parse_url(argv[ ind ], &host, &path) == -1) {
        fprintf(stderr, "Error - malformed URL '%s'.\n", argv[ 1 ]);
        return 1;
    }
    printf("Connecting to host '%s'\n", host);

    //Open Socket conection on http port
    client_connection = socket(PF_INET, SOCK_STREAM, 0);

    if (!client_connection) {
        perror("Unable to create local socket");
        return 2;
    }

    if (proxy_host) {
        printf("Connecting to host '%s'\n", proxy_host);
        host_name = gethostbyname(proxy_host);
    }

    else {
        host_name = gethostbyname(host);
    }

    if (!host_name) {
        perror("Error in name resolution");
        return 3;
    }
    host_address.sin_family = AF_INET;
    host_address.sin_port = htons(proxy_host ? proxy_port : HTTP_PORT);
    memcpy(&host_address.sin_addr, host_name->h_addr_list[ 0 ],
            sizeof ( struct in_addr));

    if (connect(client_connection, (struct sockaddr *) &host_address,
            sizeof ( host_address)) == -1) {
        perror("Unable to connect to host");
        return 4;
    }
    printf("Retrieving document: '%s'\n", path);

    http_get(client_connection, path, host, proxy_host,
            proxy_user, proxy_password);

    display_result(client_connection);

    printf("Shutting down.\n");

    if (close(client_connection) == -1) {
        perror("Error closing client connection");
        return 5;
    }

    return (EXIT_SUCCESS);
}

