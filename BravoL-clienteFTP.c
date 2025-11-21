/* * BravoL-clienteFTP.c 
 * Cliente FTP Concurrente (RFC 959)
 * * Requiere compilar junto a: 
 * connectsock.c, connectTCP.c, errexit.c, passiveTCP.c, passivesock.c
 *
 * Compilación:
 * gcc -o ftpclient BravoL-clienteFTP.c connectsock.c connectTCP.c errexit.c passiveTCP.c passivesock.c
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <netdb.h>
#include <time.h>

#define LINELEN 256

/* Prototipos de tus archivos auxiliares */
int errexit(const char *format, ...);
int connectTCP(const char *host, const char *service);
int passiveTCP(const char *service, int qlen);

/* --- Funciones Auxiliares del Cliente --- */

void limpiar_mensajes_pendientes(int s) {
    char buf[LINELEN];
    int n;
    while ((n = recv(s, buf, LINELEN - 1, MSG_DONTWAIT)) > 0) {
        buf[n] = '\0';
        printf("\n[SVR Async]: %s", buf);
    }
}

int sendCmd(int s, char *cmd, char *res) {
    int n;
    char full_cmd[LINELEN];

    /* Limpiamos respuestas viejas antes de enviar nada nuevo */
    limpiar_mensajes_pendientes(s);

    /* Formato RFC 959: <comando>\r\n */
    snprintf(full_cmd, LINELEN, "%s\r\n", cmd);
    n = write(s, full_cmd, strlen(full_cmd));
    if (n < 0) { perror("write"); return -1; }

    /* Lectura bloqueante de la respuesta inmediata */
    n = read(s, res, LINELEN - 1);
    if (n <= 0) { perror("read/server closed"); return -1; }
    res[n] = '\0';
    
    /* Feedback al usuario */
    printf("%s", res);

    /* Parsear código (primeros 3 chars) */
    int code = 0;
    if (isdigit(res[0]) && isdigit(res[1]) && isdigit(res[2])) {
        code = (res[0] - '0') * 100 + (res[1] - '0') * 10 + (res[2] - '0');
    }
    return code;
}

int negociar_pasivo(int s) {
    int sdata;
    char res[LINELEN];
    char host[64], port[8];
    int h1,h2,h3,h4,p1,p2;
    char *p;

    if (sendCmd(s, "PASV", res) != 227) {
        printf("Error: No se pudo entrar en modo pasivo.\n");
        return -1;
    }

    p = strchr(res, '(');
    if (p == NULL) return -1;
    sscanf(p+1, "%d,%d,%d,%d,%d,%d", &h1,&h2,&h3,&h4,&p1,&p2);

    snprintf(host, 64, "%d.%d.%d.%d", h1,h2,h3,h4);
    snprintf(port, 8, "%d", p1*256 + p2);

    sdata = connectTCP(host, port); /* */
    return sdata;
}

int negociar_activo(int s, int *s_listen) {
    char cmd[LINELEN], res[LINELEN];
    char my_ip[64];
    int p1, p2;
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);

    /* 1. Crear socket de escucha en puerto aleatorio */
    srand(time(NULL));
    int port_num = 20000 + (rand() % 10000); 
    char port_str[10];
    sprintf(port_str, "%d", port_num);

    *s_listen = passiveTCP(port_str, 1); /* */
    if (*s_listen < 0) return -1;

    /* 2. Obtener qué puerto se asignó realmente */
    if (getsockname(*s_listen, (struct sockaddr *)&sin, &len) == -1) {
        perror("getsockname");
        return -1;
    }

    char hostname[64];
    gethostname(hostname, sizeof(hostname));
    struct hostent *he = gethostbyname(hostname);
    
    unsigned char *ip = (unsigned char *)he->h_addr;
    
    p1 = port_num / 256;
    p2 = port_num % 256;

    /* 3. Enviar comando PORT h1,h2,h3,h4,p1,p2 */
    sprintf(cmd, "PORT %d,%d,%d,%d,%d,%d", 
            ip[0], ip[1], ip[2], ip[3], p1, p2);
    
    int code = sendCmd(s, cmd, res);
    if (code != 200) {
        printf("Error en PORT: %s", res);
        close(*s_listen);
        return -1;
    }

    return 0; 
}

/* Manejador de señal para limpiar hijos zombies */
void handle_sigchld(int sig) {
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

void ayuda() {
    printf("\n--- Cliente FTP BravoL ---\n");
    printf(" get <archivo>    : Descargar (Modo PASV - Recomendado)\n");
    printf(" put <archivo>    : Subir (Modo PASV - Recomendado)\n");
    printf(" pput <archivo>   : Subir con PORT (Modo Activo)\n");
    printf(" delete <archivo> : Borrar archivo remoto (DELE)\n");
    printf(" dir              : Listar archivos\n");
    printf(" cd <dir>         : Cambiar directorio\n");
    printf(" pwd              : Directorio actual\n");
    printf(" mkdir <dir>      : Crear directorio\n");
    printf(" quit             : Salir\n");
}

int main(int argc, char *argv[]) {
    char *host = "localhost";
    char *service = "ftp";
    char cmd[LINELEN], res[LINELEN], prompt[LINELEN];
    char data_buf[LINELEN];
    char user[64], *pass;
    char *ucmd, *arg;
    int s, sdata, s_listen, n, code;
    FILE *fp;
    pid_t pid;

    /* Configuración inicial */
    switch (argc) {
        case 1: host = "localhost"; break;
        case 3: service = argv[2]; /* FALL THROUGH */
        case 2: host = argv[1]; break;
        default:
            fprintf(stderr, "Uso: %s [host [port]]\n", argv[0]);
            exit(1);
    }

    s = connectTCP(host, service); /* */
    if (s < 0) exit(1);
    
    /* Leer banner inicial */
    n = read(s, res, LINELEN);
    if (n > 0) { res[n] = 0; printf("%s", res); }

    /* Loop de Autenticación */
    while (1) {
        printf("User: ");
        scanf("%s", user);
        sprintf(cmd, "USER %s", user);
        code = sendCmd(s, cmd, res);

        if (code == 331) {
            pass = getpass("Password: ");
            sprintf(cmd, "PASS %s", pass);
            code = sendCmd(s, cmd, res);
        }

        if (code == 230) break; 
        printf("Login incorrecto. Intente de nuevo.\n");
    }

    signal(SIGCHLD, handle_sigchld);
    fgets(prompt, sizeof(prompt), stdin); /* Limpieza buffer */
    ayuda();

    /* Loop Principal */
    while (1) {
        limpiar_mensajes_pendientes(s);

        printf("ftp> ");
        if (fgets(prompt, sizeof(prompt), stdin) == NULL) break;
        prompt[strcspn(prompt, "\n")] = 0;
        if (strlen(prompt) == 0) continue;

        ucmd = strtok(prompt, " ");

        if (strcmp(ucmd, "quit") == 0) {
            sendCmd(s, "QUIT", res);
            close(s);
            exit(0);
        } 
        else if (strcmp(ucmd, "pwd") == 0) sendCmd(s, "PWD", res);
        else if (strcmp(ucmd, "cd") == 0) {
            arg = strtok(NULL, " ");
            if (arg) { sprintf(cmd, "CWD %s", arg); sendCmd(s, cmd, res); }
            else printf("Uso: cd <directorio>\n");
        }
        else if (strcmp(ucmd, "mkdir") == 0) {
            arg = strtok(NULL, " ");
            if (arg) { sprintf(cmd, "MKD %s", arg); sendCmd(s, cmd, res); }
            else printf("Uso: mkdir <nombre>\n");
        }
        /* --- NUEVO COMANDO: DELETE --- */
        else if (strcmp(ucmd, "delete") == 0) {
            arg = strtok(NULL, " ");
            if (arg) { 
                sprintf(cmd, "DELE %s", arg); 
                sendCmd(s, cmd, res); 
            }
            else printf("Uso: delete <archivo>\n");
        }

        else if (strcmp(ucmd, "dir") == 0) {
            sdata = negociar_pasivo(s);
            if (sdata < 0) continue;

            code = sendCmd(s, "LIST", res);

            if (code == 150 || code == 125) {
                pid = fork();
                if (pid == 0) { /* HIJO */
                    close(s);
                    while ((n = recv(sdata, data_buf, sizeof(data_buf), 0)) > 0)
                        fwrite(data_buf, 1, n, stdout);
                    close(sdata);
                    exit(0);
                }
                /* PADRE */
                close(sdata);
                printf("[Info] Listado en background (PID %d)\n", pid);
            } else close(sdata);
        }
        
        else if (strcmp(ucmd, "get") == 0) {
            arg = strtok(NULL, " ");
            if (!arg) { printf("Uso: get <archivo>\n"); continue; }

            sdata = negociar_pasivo(s);
            if (sdata < 0) continue;

            sprintf(cmd, "RETR %s", arg);
            code = sendCmd(s, cmd, res);

            if (code == 150 || code == 125) {
                pid = fork();
                if (pid == 0) { /* HIJO */
                    close(s);
                    fp = fopen(arg, "wb");
                    if (!fp) { perror("Error local"); exit(1); }
                    while ((n = recv(sdata, data_buf, sizeof(data_buf), 0)) > 0)
                        fwrite(data_buf, 1, n, fp);
                    fclose(fp);
                    close(sdata);
                    exit(0);
                }
                /* PADRE */
                close(sdata);
                printf("[Info] Descarga '%s' iniciada (PID %d)\n", arg, pid);
            } else close(sdata);
        }

        else if (strcmp(ucmd, "put") == 0) {
            arg = strtok(NULL, " ");
            if (!arg) { printf("Uso: put <archivo>\n"); continue; }
            if (access(arg, R_OK) == -1) { perror("Archivo local inaccesible"); continue; }

            sdata = negociar_pasivo(s);
            if (sdata < 0) continue;

            sprintf(cmd, "STOR %s", arg);
            code = sendCmd(s, cmd, res);

            if (code == 150 || code == 125) {
                pid = fork();
                if (pid == 0) { /* HIJO */
                    close(s);
                    fp = fopen(arg, "r");
                    while ((n = fread(data_buf, 1, sizeof(data_buf), fp)) > 0)
                        send(sdata, data_buf, n, 0);
                    fclose(fp);
                    close(sdata);
                    exit(0);
                }
                /* PADRE */
                close(sdata);
                printf("[Info] Subida '%s' iniciada (PID %d)\n", arg, pid);
            } else close(sdata);
        } 
        
        else if (strcmp(ucmd, "pput") == 0) {
            arg = strtok(NULL, " ");
            if (!arg) { printf("Uso: pput <archivo>\n"); continue; }
            if (access(arg, R_OK) == -1) { perror("Archivo local inaccesible"); continue; }

            /* 1. Negociar PORT y abrir socket de escucha */
            if (negociar_activo(s, &s_listen) < 0) continue;

            /* 2. Enviar STOR */
            sprintf(cmd, "STOR %s", arg);
            code = sendCmd(s, cmd, res);

            /* 3. Aceptar conexión y transferir */
            if (code == 150 || code == 125) {
                struct sockaddr_in addr;
                socklen_t alen = sizeof(addr);
                
                /* Accept bloqueante (el servidor se conecta a nosotros) */
                sdata = accept(s_listen, (struct sockaddr *)&addr, &alen);
                close(s_listen); // Ya no necesitamos escuchar

                if (sdata < 0) {
                    perror("accept falló");
                    continue;
                }

                pid = fork();
                if (pid == 0) { /* HIJO */
                    close(s);
                    fp = fopen(arg, "r");
                    while ((n = fread(data_buf, 1, sizeof(data_buf), fp)) > 0)
                        send(sdata, data_buf, n, 0);
                    fclose(fp);
                    close(sdata);
                    exit(0);
                }
                /* PADRE */
                close(sdata);
                printf("[Info] Subida Activa (PORT) iniciada: %s (PID %d)\n", arg, pid);
            } else {
                close(s_listen);
            }
        }
        else {
            printf("Comando desconocido.\n");
        }
    }
    return 0;
}