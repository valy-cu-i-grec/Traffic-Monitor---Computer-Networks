#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <sys/select.h>

#define PORT 2909
#define MAX_CLIENTS 100
extern int errno;

typedef struct thData
{
  int idThread;
  int cl;
} thData;

typedef struct
{
  char name[64];    
  int speed;       
  int subscribed;  
  char street[100];
  int speedLim;   
} ClientInfo;

typedef struct
{
  ClientInfo info;
  int socket_fd;
  int active; 
} ConnectedClient;

typedef struct
{
  char name[100];
  int speed;
  float temp;
  char condition[32];
  char road_state[32];
} StreetInfo;

typedef struct
{
  char type[100]; // Diesel / Benzina (Standard / Premium)
  float price;
} Gas;

typedef struct
{
  char name[100];
  Gas list[4];

} GasInfo;

typedef struct
{
  char sport[32];
  char name[100];
  char time[16];
  char affected_streets[10][64];
  int num_streets;
} SportEvent;

SportEvent all_sports[50];
int total_sports_events = 0;

GasInfo all_stations[100];
int total_stations = 0;

StreetInfo all_streets[100];
int total_streets = 0;

ConnectedClient clients[MAX_CLIENTS]; // lista de broadcast
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

static void *treat(void *);
void raspunde(void *);
void load_streets();
void load_gas_stations();
void load_weather();
void load_sports();
void send_to_client(int client_fd, char *msg, int id_thread);
void handle_sign_up(int client, int id_thread, char *command);
void handle_login(int client, int id_thread, char *command, int *succes);
void handle_logout(int client, int id_thread, char *command, int *succes);
void handle_report(int client, int id_thread, char *command);
void handle_update(int client, int id_thread, char *command, time_t *last_update);
void handle_get_info(int client, int id_thread, char *command);
void handle_subscribe(int client, int id_thread, char *command);

int main()
{
  struct sockaddr_in server;
  struct sockaddr_in from;
  int sd;

  signal(SIGPIPE, SIG_IGN);

  if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
  {
    perror("[server]Eroare la socket().\n");
    return errno;
  }
  int on = 1;
  setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

  bzero(&server, sizeof(server));
  bzero(&from, sizeof(from));

  server.sin_family = AF_INET;
  server.sin_addr.s_addr = INADDR_ANY;
  server.sin_port = htons(PORT);

  if (bind(sd, (struct sockaddr *)&server, sizeof(server)) == -1)
  {
    perror("[server]Eroare la bind().\n");
    return errno;
  }

  if (listen(sd, 2) == -1)
  {
    perror("[server]Eroare la listen().\n");
    return errno;
  }

  load_streets();
  load_gas_stations();
  load_weather();
  load_sports();

  while (1)
  {
    int client;
    thData *td;
    socklen_t length = sizeof(from);

    printf("[server]Asteptam la portul %d...\n", PORT);
    fflush(stdout);

    if ((client = accept(sd, (struct sockaddr *)&from, &length)) < 0)
    {
      perror("[server]Eroare la accept().\n");
      continue;
    }

    td = (struct thData *)malloc(sizeof(struct thData));
    td->idThread = client;
    td->cl = client;

    pthread_t tid;
    if (pthread_create(&tid, NULL, &treat, td) != 0)
    {
      perror("[server] Eroare la pthread_create");
      free(td);
      close(client);
    }
  }
};

static void *treat(void *arg)
{
  struct thData tdL;
  tdL = *((struct thData *)arg);

  printf("[Thread%d]Asteptam mesajul...\n", tdL.idThread);
  fflush(stdout);
  pthread_detach(pthread_self());

  raspunde((struct thData *)arg);

  pthread_mutex_lock(&clients_mutex);
  for (int i = 0; i < MAX_CLIENTS; i++)
  {
    if (clients[i].socket_fd == tdL.cl)
    {
      clients[i].active = 0;
      clients[i].socket_fd = -1;
      memset(&clients[i].info, 0, sizeof(ClientInfo));
      break;
    }
  }
  pthread_mutex_unlock(&clients_mutex);

  close(tdL.cl);
  free(arg);
  return (NULL);
};

void raspunde(void *arg)
{
  struct thData tdL;
  tdL = *((struct thData *)arg);
  char buf[2048];

  time_t last_update = time(NULL);
  int logged = 0;

  while (1)
  {
    fd_set read_fds;
    struct timeval tv;
    FD_ZERO(&read_fds);
    FD_SET(tdL.cl, &read_fds);

    tv.tv_sec = 10;
    tv.tv_usec = 0;

    int selret = select(tdL.cl + 1, &read_fds, NULL, NULL, &tv);

    if (selret < 0)
      break;

    if (logged)
    {
      time_t now = time(NULL);
      if (now - last_update >= 70)
      {
        send_to_client(tdL.cl, "[WARNING] No speed update recieved in the last 70 seconds!", tdL.idThread);
        last_update = time(NULL);
      }
    }

    if (FD_ISSET(tdL.cl, &read_fds))
    {
      ssize_t bytes = read(tdL.cl, buf, sizeof(buf) - 1);
      if (bytes <= 0)
        break;
      buf[bytes] = '\0';

      if (strncmp(buf, "update-speed-", 13) == 0)
      {
        handle_update(tdL.cl, tdL.idThread, buf, &last_update);
        continue;
      }
      else if (strncmp(buf, "login-", 6) == 0)
      {
        if (logged)
        {
          send_to_client(tdL.cl, "Already logged in. Logout first.", tdL.idThread);
        }
        else
        {
          int succes = 0;
          handle_login(tdL.cl, tdL.idThread, buf, &succes);

          if (succes)
          {
            last_update = time(NULL);
            logged = 1;
          }
        }
      }
      else if (strncmp(buf, "logout", 6) == 0)
      {
        int succes = 0;
        handle_logout(tdL.cl, tdL.idThread, buf, &succes);
        if (succes)
        {
          logged = 0;
        }
      }
      else if (strncmp(buf, "exit", 4) == 0)
      {
        send_to_client(tdL.cl, "exit", tdL.idThread);
        break;
      }
      else if (strncmp(buf, "subscribe", 9) == 0)
      {
        handle_subscribe(tdL.cl, tdL.idThread, buf);
      }
      else if (strncmp(buf, "report-", 7) == 0)
      {
        handle_report(tdL.cl, tdL.idThread, buf);
      }
      else if (strncmp(buf, "update-", 7) == 0)
      {
        handle_update(tdL.cl, tdL.idThread, buf, &last_update);
      }
      else if (strncmp(buf, "get-info-", 9) == 0)
      {
        handle_get_info(tdL.cl, tdL.idThread, buf);
      }
      else if (strncmp(buf, "sign-up-", 8) == 0)
      {
        handle_sign_up(tdL.cl, tdL.idThread, buf);
      }

      else if (strncmp(buf, "help", 4) == 0)
      {
        char msg[4096] = "";
        snprintf(msg, sizeof(msg),
                 "\n%-20s %s\n"
                 "------------------------------------------------------------\n"
                 "%-20s <user>-<pass>\n"
                 "%-20s <user>-<pass>\n"
                 "%-20s (no params)\n"
                 "%-20s (no params)\n"
                 "%-20s (no params)\n"
                 "%-20s <street_name>\n"
                 "%-20s <value>\n"
                 "%-20s <type>-<street>\n"
                 "%-20s (no params)\n"
                 "%-20s [station | fuel]\n"
                 "%-20s [sport | street]\n"
                 "%-20s (no params)\n"
                 "------------------------------------------------------------\n"
                 "Note: Always use '-' to separate parameters.\n",
                 "COMMAND", "OPTIONS", // Primul rand (2 argumente)
                 "sign-up",            // Randurile urmatoare (cate 1 argument)
                 "login",
                 "logout",
                 "exit",
                 "subscribe",
                 "update-street",
                 "update-speed",
                 "report",
                 "get-info-weather",
                 "get-info-gas",
                 "get-info-sports",
                 "help");
        send_to_client(tdL.cl, msg, tdL.idThread);
      }
      else
      {
        char msg[4096] = "";
        snprintf(msg, sizeof(msg), "Invalid command. Please use 'help' for clarification.");
        send_to_client(tdL.cl, msg, tdL.idThread);
      }
    }
  }
}
void load_streets()
{
  FILE *f = fopen("streets.txt", "r");
  if (!f)
  {
    perror("Eroare la fopen streets!");
    return;
  }

  char line[150];
  while (fgets(line, sizeof(line), f) && total_streets < 100)
  {
    char *name = strtok(line, "~");
    char *limit = strtok(NULL, "~\n");

    if (name && limit)
    {
      strcpy(all_streets[total_streets].name, name);
      all_streets[total_streets].speed = atoi(limit);
      all_streets[total_streets].temp = 15;
      strcpy(all_streets[total_streets].condition, "senin");
      strcpy(all_streets[total_streets].road_state, "uscat");

      // printf("Am citit srada: %s, cu limita: %d\n", name, atoi(limit));
      total_streets++;
    }
  }

  printf("Am incarcat %d strazi\n", total_streets);
  fclose(f);
}

void load_gas_stations()
{
  FILE *f = fopen("gas.txt", "r");
  if (!f)
  {
    perror("eroare la fopen gas.txt");
    return;
  }
  char line[512], *saveptr_line, *saveptr_inside;

  while (fgets(line, sizeof(line), f) && total_stations < 100)
  {

    line[strcspn(line, "\r\n")] = 0;

    int index_type = 0;
    char *name = strtok_r(line, "~", &saveptr_line);
    strcpy(all_stations[total_stations].name, name);
    // printf("Am citit benzinaria %s cu urmatoarele tipuri de combustibil: \n", name);

    char *token = strtok_r(NULL, "~", &saveptr_line);
    while (token)
    {
      char *type, *price;
      type = strtok_r(token, ":", &saveptr_inside);
      price = strtok_r(NULL, ":", &saveptr_inside);

      if (type && price)
      {
        strcpy(all_stations[total_stations].list[index_type].type, type);
        float Price = atof(price);
        all_stations[total_stations].list[index_type].price = Price;
        // printf("%s, la %f RON/L\n", type, Price);
        index_type++;
      }

      token = strtok_r(NULL, "~", &saveptr_line);
    }

    total_stations++;
  }
  printf("Am incarcat %d benzinarii.\n", total_stations);
  fclose(f);
}

void load_weather()
{
  FILE *f = fopen("weather.txt", "r");

  if (!f)
  {
    perror("Eroare la fopen weather.txt");
    return;
  }

  char line[512];
  while (fgets(line, sizeof(line), f))
  {
    char *name = strtok(line, "~");
    for (int i = 0; i < total_streets; i++)
    {
      if (strcmp(name, all_streets[i].name) == 0)
      {
        char *token = strtok(NULL, "~");
        all_streets[i].temp = atof(token);
        token = strtok(NULL, "~");
        strcpy(all_streets[i].condition, token);
        token = strtok(NULL, "~\n");
        strcpy(all_streets[i].road_state, token);
      }
    }
  }
}

void load_sports()
{
  FILE *f = fopen("sports.txt", "r");

  if (!f)
  {
    perror("eroare la fopen sports.txt");
    return;
  }

  char line[128];
  while (fgets(line, sizeof(line), f) && total_sports_events < 50)
  {
    line[strcspn(line, "\r\n")] = 0;

    char *token = strtok(line, "~");
    strcpy(all_sports[total_sports_events].sport, token);
    token = strtok(NULL, "~");
    strcpy(all_sports[total_sports_events].name, token);
    token = strtok(NULL, "~");
    strcpy(all_sports[total_sports_events].time, token);
    token = strtok(NULL, "~"); // strazile afectate

    if (token)
    {
      char *saveptr_st;
      char *st_token = strtok_r(token, ",", &saveptr_st);
      int s_idx = 0;

      while (st_token && s_idx < 10)
      {
        while (*st_token == ' ')
          st_token++;

        strncpy(all_sports[total_sports_events].affected_streets[s_idx], st_token, 63);
        s_idx++;
        st_token = strtok_r(NULL, ",", &saveptr_st);
      }
      all_sports[total_sports_events].num_streets = s_idx;
    }
    total_sports_events++;
  }
  printf("Am incarcat %d evenimente sportive\n", total_sports_events);
  fclose(f);
}

void send_to_client(int client_fd, char *msg, int id_thread)
{
  if (write(client_fd, msg, strlen(msg) + 1) <= 0)
  {
    printf("[Thread %d] ", id_thread);
    perror("[Thread]Eroare la write() catre client.\n");
  }
  else
    printf("[Thread %d]Mesajul a fost trasmis cu succes.\n", id_thread);
}

int check_user_pswd(char *user, char *pswd)
{
  int status = 0;
  char *saveptr;

  pthread_mutex_lock(&file_mutex);
  FILE *f = fopen("users.txt", "r");
  if (!f)
  {
    perror("Eroare la fopen");
    pthread_mutex_unlock(&file_mutex);
    return -1;
  }

  char line[128];
  while (fgets(line, sizeof(line), f))
  {
    char *line_ptr = line;
    char *token_user = strtok_r(line_ptr, "-", &saveptr);
    char *token_pswd = strtok_r(NULL, "-\n", &saveptr);

    if (token_user && token_pswd)
    {
      if (strcmp(token_user, user) == 0 && strcmp(token_pswd, pswd) == 0)
      {
        status = 1;
        break;
      }
    }
  }

  fclose(f);
  pthread_mutex_unlock(&file_mutex);
  return status;
}

void populate_client(ClientInfo *client)
{

  unsigned int seed = time(NULL) ^ (unsigned int)pthread_self();

  int index = rand_r(&seed) % total_streets;

  strcpy(client->street, all_streets[index].name);
  client->speedLim = all_streets[index].speed;

  client->speed = rand_r(&seed) % client->speedLim; // viteza random < viteza limita

  client->subscribed = 0; // initial nu e subscribed
}

void handle_login(int client, int id_thread, char *command, int *succes)
{
  char user[64], pswd[64], msg[4096] = "", *saveptr;
  const char delim[] = "-";
  strtok_r(command, delim, &saveptr);
  char *u = strtok_r(NULL, delim, &saveptr);
  char *p = strtok_r(NULL, delim, &saveptr);

  if (u)
    strncpy(user, u, sizeof(user) - 1);
  if (p)
    strncpy(pswd, p, sizeof(pswd) - 1);
  user[63] = '\0';

  printf("Userul este: %s\nParola este: %s\n", user, pswd);
  fflush(stdout);

  pthread_mutex_lock(&clients_mutex);

  for (int i = 0; i < MAX_CLIENTS; i++)
  {
    if (clients[i].active && strcmp(user, clients[i].info.name) == 0)
    {

      pthread_mutex_unlock(&clients_mutex);

      *succes = 0;
      char msg[128];
      snprintf(msg, sizeof(msg), "User %s is already connected.", user);
      send_to_client(client, msg, id_thread);
      return;
    }
  }
  pthread_mutex_unlock(&clients_mutex);

  int rezultat = check_user_pswd(user, pswd);
  if (!rezultat)
  {
    snprintf(msg, sizeof(msg), "Incorrect user or pswd. Please try again!");
    send_to_client(client, msg, id_thread);
    *succes = 0;
    return;
  }

  else if (rezultat == 1)
  {
    *succes = 1;

    // adaug clientul in lista de broadcast
    pthread_mutex_lock(&clients_mutex);
    int i;
    for (i = 0; i < MAX_CLIENTS; i++)
    {
      if (clients[i].active == 0)
      {
        clients[i].socket_fd = client;
        strcpy(clients[i].info.name, user);
        clients[i].active = 1;
        break;
      }
    }

    // aici populam membrii din structura ClientInfo

    populate_client(&clients[i].info);

    pthread_mutex_unlock(&clients_mutex);

    snprintf(msg, sizeof(msg), "Login succeeded. Welcome %s!", user);
    send_to_client(client, msg, id_thread);
  }

  else if (rezultat == -1)
  {
    snprintf(msg, sizeof(msg), "Login failed. Please try again!");
    send_to_client(client, msg, id_thread);
  }
}

void handle_subscribe(int client, int id_thread, char *command)
{
  int found = 0;
  char msg[200];

  pthread_mutex_lock(&clients_mutex);

  for (int i = 0; i < MAX_CLIENTS; i++)
  {
    if (clients[i].active == 1 && client == clients[i].socket_fd && clients[i].info.subscribed == 0)
    {
      clients[i].info.subscribed = 1;
      found = 1;
      break;
    }
  }

  pthread_mutex_unlock(&clients_mutex);
  if (found)
    snprintf(msg, sizeof(msg), "Subscribe succesful. You will now be able to recieve informations about sport events, gas prices and weather.");
  else
    snprintf(msg, sizeof(msg), "Error. You are not logged in or you are already subscribed!");

  send_to_client(client, msg, id_thread);
}

int check_user(char *user)
{

  int status = 0;
  char *saveptr;

  pthread_mutex_lock(&file_mutex);
  FILE *f = fopen("users.txt", "r");
  if (!f)
  {
    perror("Eroare la fopen");
    pthread_mutex_unlock(&file_mutex);
    return -1;
  }

  char line[128];
  while (fgets(line, sizeof(line), f))
  {
    char *line_ptr = line;
    char *token_user = strtok_r(line_ptr, "-", &saveptr);
    strtok_r(NULL, "-\n", &saveptr);

    if (token_user)
    {
      if (strcmp(token_user, user) == 0)
      {
        status = 1;
        break;
      }
    }
  }

  fclose(f);
  pthread_mutex_unlock(&file_mutex);
  return status;
}

int add_user(char *user, char *pswd)
{
  pthread_mutex_lock(&file_mutex);
  FILE *f = fopen("users.txt", "a");

  if (!f)
  {
    pthread_mutex_unlock(&file_mutex);
    return 0;
  }

  char msg[130];
  snprintf(msg, sizeof(msg), "%s-%s\n", user, pswd);
  if (fprintf(f, "%s", msg) <= 0)  
  {
    pthread_mutex_unlock(&file_mutex);
    fclose(f);
    return 0;
  }

  pthread_mutex_unlock(&file_mutex);
  fclose(f);
  return 1;
}

void handle_sign_up(int client, int id_thread, char *command)
{
  char user[64], pswd[64], msg[4096] = "", *saveptr;
  const char delim[] = "-";
  strtok_r(command, delim, &saveptr);
  strtok_r(NULL, delim, &saveptr);
  char *u = strtok_r(NULL, delim, &saveptr);
  char *p = strtok_r(NULL, delim, &saveptr);

  if (u)
    strcpy(user, u);
  if (p)
    strcpy(pswd, p);

  if (check_user(user) == 1) // am gasit deja acest user sau parola in fisier
  {
    snprintf(msg, sizeof(msg), "User already exists. Try another username");
    send_to_client(client, msg, id_thread);
  }
  else
  {
    if (add_user(user, pswd) == 1)
    {
      send_to_client(client, "sign-up succesful!\n", id_thread);
    }
    else
      send_to_client(client, "sign-up failed!\n", id_thread);
  }
}

void handle_logout(int client, int id_thread, char *command, int *succes)
{
  // elimin din vectorul global
  int found = 0;
  pthread_mutex_lock(&clients_mutex);

  for (int i = 0; i < MAX_CLIENTS; i++)
  {
    if (clients[i].active == 1 && client == clients[i].socket_fd)
    {
      memset(&clients[i].info, 0, sizeof(ClientInfo));

      clients[i].socket_fd = -1;
      clients[i].active = 0;

      found = 1;
      break;
    }
  }
  pthread_mutex_unlock(&clients_mutex);

  if (found)
  {
    send_to_client(client, "Logout succesful. Goodbye!", id_thread);
    *succes = 1;
  }
  else
  {
    send_to_client(client, "Logout failed. You are not logged in!", id_thread);
    *succes = 0;
  }
}

void handle_update(int client, int id_thread, char *command, time_t *last_update)
{
  // verific daca am update-speed sau update-road
  char *saveptr;
  const char delim[] = "-";

  strtok_r(command, delim, &saveptr);            // update
  char *token = strtok_r(NULL, delim, &saveptr); // speed sau street
  if(token == NULL) return;

  if (strcmp(token, "speed") == 0)
  {
    char *s = strtok_r(NULL, delim, &saveptr);
    int Speed = atoi(s);
    int found = 0;
    char final_msg[512] = "";

    pthread_mutex_lock(&clients_mutex);
    // caut descriptorul in lista de clienti
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
      if (clients[i].active == 1 && client == clients[i].socket_fd)
      {
        clients[i].info.speed = Speed;
        found = 1;

        if (Speed > clients[i].info.speedLim)
        {
          snprintf(final_msg, sizeof(final_msg), "Alert! You are currently exceeding the speed limit on %s (%d km/h). Please slow down!  ", clients[i].info.street, clients[i].info.speedLim);
        }
        break;
      }
    }
    pthread_mutex_unlock(&clients_mutex);

    if (found)
    {
      *last_update = time(NULL);
      strncat(final_msg, "Update recieved. Thank you!", sizeof(final_msg) - strlen(final_msg) - 1);
      send_to_client(client, final_msg, id_thread);
    }
    else
    {
      send_to_client(client, "Update failed. You must be logged in to update your speed!", id_thread);
    }
  }

  else if (strcmp(token, "street") == 0)
  {
    char *s = strtok_r(NULL, delim, &saveptr); // street name
    if (s == NULL)
    {
      send_to_client(client, "Please type a valid street name atfer command update-street-", id_thread);
      return;
    }

    char final_msg[512] = "";

    int found_street = 0;
    int found_client = 0;

    int new_limit = 50; 
    for (int j = 0; j < total_streets; j++)
    {
      if (strcmp(all_streets[j].name, s) == 0)
      {
        new_limit = all_streets[j].speed;
        found_street = 1;
        break;
      }
    }

    if (!found_street)
    {
      send_to_client(client, "Street not found in database. Please insert a valid street!", id_thread);
      return;
    }

    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
      if (clients[i].active && client == clients[i].socket_fd)
      {
        // verific sa nu fie aceeasi strada
        if (strcmp(s, clients[i].info.street) == 0)
        {
          snprintf(final_msg, sizeof(final_msg), "You are already on street %s. Updated street must be different from the current street!  ", clients[i].info.street);
          send_to_client(client, final_msg, id_thread);
          pthread_mutex_unlock(&clients_mutex);
          return;
        }
        else
        {
          strcpy(clients[i].info.street, s);
          clients[i].info.speedLim = new_limit;
        }
        found_client = 1;
        break;
      }
    }
    pthread_mutex_unlock(&clients_mutex);

    if (found_client)
    {
      *last_update = time(NULL);
      snprintf(final_msg, sizeof(final_msg), "Street updated to %s (Limit: %d km/h).", s, new_limit);
      send_to_client(client, final_msg, id_thread);
    }
    else
    {
      send_to_client(client, "Update failed. You are not logged in!", id_thread);
    }
  }
  else
  {
    send_to_client(client, "Invalid option for update. Usage: update-speed-<val> or update-street-<name>!", id_thread);
  }
}

void handle_get_info(int client, int id_thread, char *command) // gestioneaza informatiile cu subscriptie, dar si strada userului si viteza sa actuala
{
  char *token, *saveptr;
  strtok_r(command, "-", &saveptr);      // get
  strtok_r(NULL, "-", &saveptr);         // info
  token = strtok_r(NULL, "-", &saveptr); // optiunea aleasa

  if (token == NULL)
  {
    send_to_client(client, "Usage: get-info-<option> (options: street, speed, gas, sports)", id_thread);
    return;
  }

  // verifiy subscription
  int subbed = 0;
  pthread_mutex_lock(&clients_mutex);
  for (int i = 0; i < MAX_CLIENTS; i++)
  {
    if (clients[i].active && client == clients[i].socket_fd)
    {
      subbed = clients[i].info.subscribed;
      break;
    }
  }
  pthread_mutex_unlock(&clients_mutex);

  if (strncmp(token, "street", 6) == 0)
  {
    int found = 0;
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
      if (clients[i].active == 1 && client == clients[i].socket_fd)
      {
        char msg[512];
        snprintf(msg, sizeof(msg), "Street: %s. Speed limit: %d km/h", clients[i].info.street, clients[i].info.speedLim);
        send_to_client(client, msg, id_thread);
        found = 1;
        break;
      }
    }
    pthread_mutex_unlock(&clients_mutex);

    if (!found)
      send_to_client(client, "You are not logged in.", id_thread);
  }
  else if (strncmp(token, "speed", 5) == 0)
  {
    int found = 0;
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
      if (clients[i].active && client == clients[i].socket_fd)
      {
        char msg[100];
        snprintf(msg, sizeof(msg), "Your speed: %d", clients[i].info.speed);
        send_to_client(client, msg, id_thread);
        found = 1;
        break;
      }
    }

    pthread_mutex_unlock(&clients_mutex);

    if (!found)
      send_to_client(client, "You are not logged in.", id_thread);
  }
  else if (strncmp(token, "gas", 3) == 0)
  {
    if (subbed)
    {
      char *filter = saveptr;
      char final_msg[4096] = "\n";
      int found_any = 0;

      if (filter == NULL || strlen(filter) == 0)
      {
        for (int i = 0; i < total_stations; i++)
        {
          strcat(final_msg, all_stations[i].name);
          strcat(final_msg, ":\n");

          for (int j = 0; j < 4; j++)
          {
            char p_buf[512];
            snprintf(p_buf, sizeof(p_buf), "  %s: %.2f RON/L\n",
                     all_stations[i].list[j].type, all_stations[i].list[j].price);
            strcat(final_msg, p_buf);
          }
          strcat(final_msg, "\n");
        }
        found_any = 1;
      }
      else
      {
        for (int i = 0; i < total_stations; i++)
        {
          if (strcasecmp(all_stations[i].name, filter) == 0)
          {
            snprintf(final_msg, sizeof(final_msg), "\nPrices %s\n", all_stations[i].name);
            for (int j = 0; j < 4; j++)
            {
              char p_buf[512];
              snprintf(p_buf, sizeof(p_buf), "  %-20s: %.2f RON/L\n",
                       all_stations[i].list[j].type, all_stations[i].list[j].price);
              strcat(final_msg, p_buf);
            }
            found_any = 1;
            break;
          }
        }
        if (!found_any)
        {
          snprintf(final_msg, sizeof(final_msg), "\nPrices %s\n", filter);
          for (int i = 0; i < total_stations; i++)
          {
            for (int j = 0; j < 4; j++)
            {
              if (strcasecmp(all_stations[i].list[j].type, filter) == 0)
              {
                char p_buf[128];
                snprintf(p_buf, sizeof(p_buf), " %-12s : %.2f RON/L\n",
                         all_stations[i].name, all_stations[i].list[j].price);
                strcat(final_msg, p_buf);
                found_any = 1;
              }
            }
          }
        }
      }
      if (found_any)
      {
        send_to_client(client, final_msg, id_thread);
      }
      else
      {
        send_to_client(client, "Error: No gas station or fuel type matched yout filter.", id_thread);
      }
    }
    else
    {
      send_to_client(client, "You are not subscribed! Use command subscribe.", id_thread);
    }
  }
  else if (strncmp(token, "weather", 7) == 0)
  {
    if (subbed)
    {
      int done = 0;
      pthread_mutex_lock(&clients_mutex);
      for (int i = 0; i < MAX_CLIENTS; i++)
      {
        if (clients[i].active && clients[i].socket_fd == client)
        {
          for (int j = 0; j < total_streets; j++)
          {
            if (strcmp(clients[i].info.street, all_streets[j].name) == 0)
            {
              char msg[128];
              snprintf(msg, sizeof(msg), "\nTemp: %.1f\nCondition: %s\nRoad state: %s", all_streets[j].temp, all_streets[j].condition, all_streets[j].road_state);
              pthread_mutex_unlock(&clients_mutex);
              send_to_client(client, msg, id_thread);
              done = 1;
              break;
            }
          }
          if (done)
            break;
        }
      }
    }
    else
    {
      send_to_client(client, "You are not subscribed! Use command subscribe.", id_thread);
    }
  }
  else if (strncmp(token, "sports", 6) == 0)
  {
    if (subbed)
    {
      char *f1 = strtok_r(NULL, "-", &saveptr);
      char *f2 = strtok_r(NULL, "-", &saveptr);

      char final_msg[4096] = "\nSport events:\n";
      int found_any = 0;

      for (int i = 0; i < total_sports_events; i++)
      {
        int match_f1 = (f1 == NULL);
        int match_f2 = (f2 == NULL);

        if (f1 != NULL)
        {
          if (strcasecmp(all_sports[i].sport, f1) == 0)
            match_f1 = 1;

          for (int k = 0; k < all_sports[i].num_streets; k++)
          {
            if (strcasecmp(all_sports[i].affected_streets[k], f1) == 0)
            {
              match_f1 = 1;
              break;
            }
          }
        }

        if (f2 != NULL)
        {
          if (strcasecmp(all_sports[i].sport, f2) == 0)
            match_f2 = 1;

          for (int k = 0; k < all_sports[i].num_streets; k++)
          {
            if (strcasecmp(all_sports[i].affected_streets[k], f2) == 0)
            {
              match_f2 = 1;
              break;
            }
          }
        }

        if (match_f1 && match_f2)
        {
          char event_buf[512];

          snprintf(event_buf, sizeof(event_buf), "[%s] %s (%s)\n   Affected streets: ",
                   all_sports[i].sport, all_sports[i].name, all_sports[i].time);
          strcat(final_msg, event_buf);

          for (int k = 0; k < all_sports[i].num_streets; k++)
          {
            strcat(final_msg, all_sports[i].affected_streets[k]);
            if (k < all_sports[i].num_streets - 1)
              strcat(final_msg, ", ");
          }
          strcat(final_msg, "\n");
          found_any = 1;
        }
      }

      if (found_any)
        send_to_client(client, final_msg, id_thread);
      else
        send_to_client(client, "No sports events found for those criteria", id_thread);
    }
    else
    {
      send_to_client(client, "You are not subscribed! Use command subscribe.", id_thread);
    }
  }
  else
  {
    send_to_client(client, "Invalid option for get-info. Use 'help' for clarification.\n", id_thread);
  }
}

void handle_report(int client, int id_thread, char *command)
{
  char *saveptr;
  strtok_r(command, "-", &saveptr);
  char *type = strtok_r(NULL, "-", &saveptr);
  char *target_street = strtok_r(NULL, "-", &saveptr);

  if (type == NULL || target_street == NULL)
  {
    send_to_client(client, "Usage: report-<type>-<street_name>", id_thread);
    return;
  }

  if (strcmp(type, "police") != 0 && strcmp(type, "pothole") != 0 && strcmp(type, "crash") != 0 && strcmp(type, "reparations") != 0 && strcmp(type, "jam") != 0)
  {
    send_to_client(client, "Invalid report type. Type can only be: police, pothole, crash, reparations", id_thread);
    return;
  }

  int street_exists = 0;
  for (int j = 0; j < total_streets; j++)
  {
    if (strcmp(all_streets[j].name, target_street) == 0)
    {
      street_exists = 1;
      break;
    }
  }

  if (!street_exists)
  {
    send_to_client(client, "Error: Street not found in database!", id_thread);
    return;
  }

  char reporter_name[64] = "Anonim";
  char alert_msg[512];
  char advice_msg[512];

  int suggested_speed = 50;
  if (strcmp(type, "crash") == 0)
    suggested_speed = 10;
  else if (strcmp(type, "reparations") == 0)
    suggested_speed = 30;
  else if (strcmp(type, "pothole") == 0)
    suggested_speed = 30;
  else if (strcmp(type, "jam") == 0)
    suggested_speed = 10;

  pthread_mutex_lock(&clients_mutex);

  for (int i = 0; i < MAX_CLIENTS; i++)
  {
    if (clients[i].active && clients[i].socket_fd == client)
    {
      strcpy(reporter_name, clients[i].info.name);
      break;
    }
  }

  snprintf(alert_msg, sizeof(alert_msg),
           "\n[TRAFFIC ALERT] User %s reported %s on %s! Drive carefully!",
           reporter_name, type, target_street);

  snprintf(advice_msg, sizeof(advice_msg),
           "\n[TRAFFIC ADVICE] You are currently on a street with an incident (%s). Recommended speed limit: %d km/h!",
           type, suggested_speed);

  int notified_count = 0;
  for (int i = 0; i < MAX_CLIENTS; i++)
  {
    if (clients[i].active && clients[i].socket_fd != client)
    {
      send_to_client(clients[i].socket_fd, alert_msg, id_thread);
      if (strcmp(clients[i].info.street, target_street) == 0)
      {
        send_to_client(clients[i].socket_fd, advice_msg, id_thread);
      }
      notified_count++;
    }
  }
  pthread_mutex_unlock(&clients_mutex);

  char msg[128];
  snprintf(msg, sizeof(msg), "Report sent to %d drivers!", notified_count);
  send_to_client(client, msg, id_thread);
}
