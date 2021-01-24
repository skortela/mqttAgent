/*
 *
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
//#include <syslog.h>
#include <signal.h>
#include <getopt.h>
#include <string.h>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <time.h>

#include <ctype.h>

#include <MQTTClient.h>
#include <MQTTClientPersistence.h>

#include <mysql/mysql.h>

#include <curl/curl.h>

#include <map>

const char* KDefaultConfFile = "/etc/mqttAgent/mqttAgent.conf";

#define UNUSED(x) (void)(x)

struct Config {
    char* mqttHost;
    char* mqttUsername;
    char* mqttPassword;

    char* dbHost;
    int   dbPort;
    char* dbUsername;
    char* dbPassword;
    char* dbName;

    char* fileUploadHandler;
    char* fileUploadKeyHeader;

    char* clientId;
    char* topic;
} config;


#define QOS         1
#define TIMEOUT 10000L

static int running = 0;
//static int counter = 0;
static char *conf_file_name = NULL;
static char *pid_file_name = NULL;
static int pid_fd = -1;
static char *app_name = NULL;
static char* log_file_name = NULL;

struct cmp_str
{
   bool operator()(char const *a, char const *b) const
   {
      return std::strcmp(a, b) < 0;
   }
};

static std::map<char*, int, cmp_str> cachedTopics;

void DBG(const char *str,...)
{
    FILE* file = NULL;
    FILE* stream = NULL;
    if (log_file_name)
		file = fopen(log_file_name, "a+");
    if (file)
        stream = file;
    else
        stream = stdout;

    char buff[30];
    struct tm my_time;
    time_t now;
    now = time(NULL);
    my_time = *(localtime(&now));
    size_t len = strftime(buff, sizeof(buff),"%d-%m-%Y %H:%M:%S ", &my_time);

    len = fwrite(buff, len, 1, stream);
    if (len > 0) {
        //fprintf(stream,"%d : ",time(NULL));
        va_list arglist;
        va_start(arglist,str);
        vfprintf(stream,str,arglist);
        va_end(arglist);
        fprintf(stream," \n");

        fflush(stream);
    }
    if (file)
        fclose(file);
}

int getTopicIdFromCache(const char* topic)
{
    int topicId = -1;
    std::map<char*,int>::const_iterator it;
    it = cachedTopics.find((char*)topic);
    if (it != cachedTopics.end()) {
        topicId = it->second;
        //DBG("getTopicIdFromCache %s, found: %d", topic, topicId);
    }
    /*else {
        DBG("getTopicIdFromCache %s, not found!", topic);
    }*/
    return topicId;
}

void resetTopicCache()
{
    std::map<char*,int>::iterator it = cachedTopics.begin();
    if (it != cachedTopics.end())
    {
        //DBG("resetTopicCache: %s", it->first);
        // found it - delete it
        free(it->first);
        cachedTopics.erase(it);
    }
}

// Adds topic to cache, copies topic string
void addTopicToCache(const char* topic, int topicId)
{
    //DBG("addTopicToCache: %s, %d", topic, topicId);
    cachedTopics[strdup(topic)] = topicId;
}

MYSQL* SqlConnect();


int isConfigValid()
{
    if (config.mqttHost
        && config.mqttUsername
        && config.mqttPassword
        && config.dbHost
        && config.dbPort
        && config.dbUsername
        && config.dbPassword
        && config.dbName
        && config.fileUploadHandler
        && config.fileUploadKeyHeader
        && config.clientId
        && config.topic)
        return 1;
    else
        return 0;
}

// Note: This function returns a pointer to a substring of the original string.
// If the given string was allocated dynamically, the caller must not overwrite
// that pointer with the returned value, since the original pointer must be
// deallocated using the same allocator with which it was allocated.  The return
// value must NOT be deallocated using free() etc.
char* trim(char *str)
{
  char* end;

  // Trim leading space
  while(isspace((unsigned char)*str)) str++;

  if(*str == 0)  // All spaces?
    return str;

  // Trim trailing space
  end = str + strlen(str) - 1;
  while(end > str && isspace((unsigned char)*end)) end--;

  // Write new null terminator
  *(end+1) = 0;

  return str;
}
/**
 * \brief This function tries to test config file
 */
int test_conf_file(char *_conf_file_name)
{
	FILE *conf_file = NULL;
	int ret = EXIT_SUCCESS;

	conf_file = fopen(_conf_file_name, "r");

	if (conf_file == NULL) {
		fprintf(stderr, "Can't read config file %s\n",
			_conf_file_name);
		return EXIT_FAILURE;
	}

    const int KBufferSize = 1024;
    char buf[KBufferSize];
    int linenum = 0;
    char* currentTag = NULL;

    while(fgets(buf, KBufferSize, conf_file)!=NULL) {
        linenum++;
        //printf("%s", buf);
        char* pTxt = trim(buf);
        if (strlen(pTxt) == 0)
            continue;
        if (pTxt[0] == '#')
            continue;

        if (pTxt[0] == '[') {
            char * pch = strchr(pTxt, ']');
            if (!pch) {
                // log failure
                ret = EXIT_FAILURE;
                DBG("Failed to parse configuretion file!");
                break;
            }
            pch[0] = '\0';
            if (currentTag)
                free(currentTag);
            currentTag = strdup(pTxt+1);
            //printf("currentTag: '%s'\n", currentTag);
            continue;
        }


        char * pch = strchr(pTxt, '=');
        if (pch) {
            char* key = pTxt;
            key[pch-pTxt] = '\0';
            char* value = pch+1;

            key = trim(key);
            value = trim(value);
            printf("key: '%s'\n", key);
            printf("value: %s\n", value);

            if (strcmp(key, "clientid") == 0) {
                config.clientId = strdup(value);
            }
            else if (strcmp(key, "listentopic") == 0)
                config.topic = strdup(value);
            else if (currentTag && strcmp(currentTag, "MQTT") == 0) {
                if (strcmp(key, "host") == 0)
                    config.mqttHost = strdup(value);

                else if (strcmp(key, "username") == 0)
                    config.mqttUsername = strdup(value);
                else if (strcmp(key, "password") == 0)
                    config.mqttPassword = strdup(value);
            }
            else if (currentTag && strcmp(currentTag, "DATABASE") == 0) {
                if (strcmp(key, "host") == 0) {
                    config.dbHost = strdup(value);
                }
                else if (strcmp(key, "port") == 0) {
                   config.dbPort = atoi(value);
                   if (config.dbPort == 0) {
                       ret = EXIT_FAILURE;
                       DBG("Failed to parse integer from 'port'");
                   }
                }
                else if (strcmp(key, "username") == 0)
                    config.dbUsername = strdup(value);
                else if (strcmp(key, "password") == 0)
                    config.dbPassword = strdup(value);
                else if (strcmp(key, "database") == 0)
                    config.dbName = strdup(value);
            }
            else if (currentTag && strcmp(currentTag, "FILEUPLOAD") == 0) {
                if (strcmp(key, "handlerurl") == 0)
                    config.fileUploadHandler = strdup(value);
                else if (strcmp(key, "extraheader") == 0)
                    config.fileUploadKeyHeader = strdup(value);
            }
        }
    }

    if (currentTag)
        free(currentTag);

	fclose(conf_file);

    if (!isConfigValid()) {
        printf("config is not valid!\n");
        ret = EXIT_FAILURE;
    }

    printf("dbhost: %s\n", config.dbHost);
    //config.dbHost, config.dbUsername

    printf("Testing db connection..\n");
    MYSQL* sql = SqlConnect();
    if (sql) {
        printf(" SUCCEEDED\n");
        mysql_close(sql);
    }
    else {
        ret = EXIT_FAILURE;
        printf(" FAILED!\n");
    }


	return ret;
}
/**
 * \brief Read configuration from config file
 */
int read_conf_file(int reload)
{
	FILE *conf_file = NULL;
	int ret = 0;

    const char* filename = conf_file_name;

	if (!filename) {
        DBG("Config not defined, using defauft: %s", KDefaultConfFile);
        filename = KDefaultConfFile;
    }

	conf_file = fopen(filename, "r");

	if (conf_file == NULL) {
		DBG("Can not open config file: %s, error: %s", filename, strerror(errno));
		return -1;
	}

    const int KBufferSize = 1024;
    char buf[KBufferSize];
    int linenum = 0;
    char* currentTag = NULL;

    while(fgets(buf, KBufferSize, conf_file)!=NULL) {
        linenum++;
        //printf("%s", buf);
        char* pTxt = trim(buf);
        if (strlen(pTxt) == 0)
            continue;
        if (pTxt[0] == '#')
            continue;

        if (pTxt[0] == '[') {
            char * pch = strchr(pTxt, ']');
            if (!pch) {
                // log failure
                ret = EXIT_FAILURE;
                DBG("Error detected on config file on line: %d", linenum);
                break;
            }
            pch[0] = '\0';
            if (currentTag)
                free(currentTag);
            currentTag = strdup(pTxt+1);
            continue;
        }


        char * pch = strchr(pTxt, '=');
        if (pch) {
            char* key = pTxt;
            key[pch-pTxt] = '\0';
            char* value = pch+1;

            key = trim(key);
            value = trim(value);
            //printf("key: '%s'\n", key);
            //printf("value: %s\n", value);

            if (strcmp(key, "clientid") == 0) {
                config.clientId = strdup(value);
            }
            else if (strcmp(key, "listentopic") == 0)
                config.topic = strdup(value);
            else if (strcmp(key, "logfile") == 0) {
                if (!log_file_name)
                    log_file_name = strdup(value);
            }
            else if (currentTag && strcmp(currentTag, "MQTT") == 0) {
                if (strcmp(key, "host") == 0)
                    config.mqttHost = strdup(value);

                else if (strcmp(key, "username") == 0)
                    config.mqttUsername = strdup(value);
                else if (strcmp(key, "password") == 0)
                    config.mqttPassword = strdup(value);
            }
            else if (currentTag && strcmp(currentTag, "DATABASE") == 0) {
                if (strcmp(key, "host") == 0) {
                    config.dbHost = strdup(value);
                }
                else if (strcmp(key, "port") == 0) {
                   config.dbPort = atoi(value);
                   if (config.dbPort == 0) {
                       ret = EXIT_FAILURE;
                       DBG("Line: %d: Failed to parse integer from 'port'", linenum);
                   }
                }
                else if (strcmp(key, "username") == 0)
                    config.dbUsername = strdup(value);
                else if (strcmp(key, "password") == 0)
                    config.dbPassword = strdup(value);
                else if (strcmp(key, "database") == 0)
                    config.dbName = strdup(value);
            }
            else if (currentTag && strcmp(currentTag, "FILEUPLOAD") == 0) {
                if (strcmp(key, "handlerurl") == 0)
                    config.fileUploadHandler = strdup(value);
                else if (strcmp(key, "extraheader") == 0)
                    config.fileUploadKeyHeader = strdup(value);
            }
        }
    }

    if (currentTag)
        free(currentTag);

    fclose(conf_file);

	if (ret == 0) {
		if (reload == 1) {
			DBG("Reloaded configuration file %s", filename);
		}
	}

    if (!isConfigValid()) {
        DBG("Config is not valid!");
        ret = -1;
    }

	return ret;
}


/**
 * \brief Callback function for handling signals.
 * \param	sig	identifier of signal
 */
void handle_signal(int sig)
{
	if (sig == SIGINT) {
		DBG("SIGINT: stopping mqttAgent ...");
		/* Unlock and close lockfile */
		if (pid_fd != -1) {
			int rc = lockf(pid_fd, F_ULOCK, 0);
            UNUSED(rc);
			close(pid_fd);
		}
		/* Try to delete lockfile */
		if (pid_file_name != NULL) {
			unlink(pid_file_name);
		}
		running = 0;
		/* Reset signal handling to default behavior */
		signal(SIGINT, SIG_DFL);
	} else if (sig == SIGHUP) {
		DBG("SIGHUP: reloading mqttAgent config file ...");
		read_conf_file(1);
	} else if (sig == SIGCHLD) {
		DBG("SIGCHLD: received SIGCHLD signal");
	}
}

/**
 * \brief This function will daemonize this app
 */
static void daemonize()
{
	pid_t pid = 0;
	int fd;

	/* Fork off the parent process */
	pid = fork();

	/* An error occurred */
	if (pid < 0) {
		exit(EXIT_FAILURE);
	}

	/* Success: Let the parent terminate */
	if (pid > 0) {
		exit(EXIT_SUCCESS);
	}

	/* On success: The child process becomes session leader */
	if (setsid() < 0) {
		exit(EXIT_FAILURE);
	}

	/* Ignore signal sent from child to parent process */
	signal(SIGCHLD, SIG_IGN);

	/* Fork off for the second time*/
	pid = fork();

	/* An error occurred */
	if (pid < 0) {
		exit(EXIT_FAILURE);
	}

	/* Success: Let the parent terminate */
	if (pid > 0) {
		exit(EXIT_SUCCESS);
	}

	/* Set new file permissions */
	umask(0);

	/* Change the working directory to the root directory */
	/* or another appropriated directory */
	int rc = chdir("/");
    UNUSED(rc);

	/* Close all open file descriptors */
	for (fd = sysconf(_SC_OPEN_MAX); fd > 0; fd--) {
		close(fd);
	}

	/* Reopen stdin (fd = 0), stdout (fd = 1), stderr (fd = 2) */
	stdin = fopen("/dev/null", "r");
	stdout = fopen("/dev/null", "w+");
	stderr = fopen("/dev/null", "w+");

	/* Try to write PID of daemon to lockfile */
	if (pid_file_name != NULL)
	{
		char str[256];
		pid_fd = open(pid_file_name, O_RDWR|O_CREAT, 0640);
		if (pid_fd < 0) {
			/* Can't open lockfile */
            DBG("Failed to open lockfile %s", pid_file_name);
			exit(EXIT_FAILURE);
		}
		if (lockf(pid_fd, F_TLOCK, 0) < 0) {
			/* Can't lock file */
            DBG("Can't lock file %s", pid_file_name);
			exit(EXIT_FAILURE);
		}
		/* Get current PID */
		sprintf(str, "%d\n", getpid());
		/* Write PID to lockfile */
		rc = write(pid_fd, str, strlen(str));
        UNUSED(rc);
	}
}


void printErr(MYSQL *con) {
    DBG("SQL error: %s", mysql_error(con));
}

int isNumeric(const char* value) {
    size_t i;
    for (i = 0; i < strlen(value); i++) {
        if (i == 0 && (value[i] == '-' || value[i] == '+'))
            continue;
        if (value[i] == '.')
            continue;
        if (!isdigit(value[i]))
            return 0;
    }
    return 1;
}

int sqlCreateNewTopic(MYSQL *sql, const char* topic) {

    const char* KInsertQuery = "INSERT INTO iot_topic (topic) VALUES(\"%s\")";
    const int bufferSize = strlen(KInsertQuery) + strlen(topic);
    char buffer[bufferSize];
    int cx = snprintf(buffer, bufferSize, KInsertQuery, topic);
    if (cx < 0) {
        return -1;
    }

    if (mysql_query(sql, buffer)) {
        printErr(sql);
        return -1;
    }

    int id = mysql_insert_id(sql);
    return id;
}


//#ifdef nodef
struct dataStruct {
    void* dataPtr;
    size_t bytesLeft;
};

static size_t read_callback(void *ptr, size_t size, size_t nmemb, void *userp)
{
    size_t dataLen = size*nmemb;

    struct dataStruct *data = (struct dataStruct *)userp;

    if (dataLen > data->bytesLeft)
        dataLen = data->bytesLeft;


    memcpy(ptr, data->dataPtr, dataLen);

    data->dataPtr = (char*)data->dataPtr + dataLen;
    data->bytesLeft -= dataLen;

    return dataLen;
}

int sendFile(const char* filename, void* data, int size) {
    int res = -1;
    /* get a curl handle */
    CURL* curl = curl_easy_init();
    if(curl) {
        /* we want to use our own read function */
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);

        /* enable uploading */
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

        /* HTTP PUT please */
        curl_easy_setopt(curl, CURLOPT_PUT, 1L);

        /* specify target URL, and note that this URL should include a file
        name, not only a directory */
        char url[1024];
        strcpy(url, config.fileUploadHandler);
        strcat(url, filename);

        curl_easy_setopt(curl, CURLOPT_URL, url);

        struct curl_slist *list = NULL;
        list = curl_slist_append(list, config.fileUploadKeyHeader);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);

        struct dataStruct userdata;
        userdata.dataPtr = data;
        userdata.bytesLeft = size;

        /* now specify which file to upload */
        curl_easy_setopt(curl, CURLOPT_READDATA, &userdata);

        /* provide the size of the upload, we specicially typecast the value
        to curl_off_t since we must be sure to use the correct data size */
        curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE,
                     (curl_off_t)size);

        /* Now run off and do what you've been told! */
        int res = curl_easy_perform(curl);

        curl_slist_free_all(list); /* free the list again */

        /* Check for errors */
        if(res != CURLE_OK) {
            DBG("curl upload failed!");
            //fprintf(stderr, "curl_easy_perform() failed: %s\n",
            //  curl_easy_strerror(res));
        }

        /* always cleanup */
        curl_easy_cleanup(curl);
    }
    else {
        DBG("curl init failed!");
    }
    return res;
}

int getTopicIdFromSql(MYSQL *sql, const char* topic)
{
    //DBG("getTopicIdFromSql topic: %s", topic);
    int topicId = -1;
    const char* KTopicQuery = "SELECT topic_id FROM iot_topic WHERE topic = \"%s\" LIMIT 1";
    const int topicQuerySize = strlen(KTopicQuery) + strlen(topic);
    char topicQuery[topicQuerySize];
    int cx = snprintf(topicQuery, topicQuerySize, KTopicQuery, topic);
    if (cx < 0) {
        return -2; // fatal error
    }
    int err = mysql_query(sql, topicQuery);

    if (!err) {
        MYSQL_RES *result = mysql_store_result(sql);
        if (result)
        {
            MYSQL_ROW row = mysql_fetch_row(result);
            if (row) {
                //DBG("found topic_id: %s", row[0]);
                topicId = atoi(row[0]);
            }
            mysql_free_result(result);
        }
    }
    else {
        printErr(sql);
    }
    return topicId;
}
int sql_insert_topicdata(MYSQL *sql, int topicId, const char* value) {
    if (topicId < 0) {
        return topicId;
    }

    const char* columnName;
    if (isNumeric(value))
        columnName = "value";
    else
        columnName = "value_txt";

    /*const char* KTopicQuery = "SELECT topic_id FROM iot_topic WHERE topic = \"%s\" LIMIT 1";
    const int topicQuerySize = strlen(KTopicQuery) + strlen(topic);
    char topicQuery[topicQuerySize];
    int cx = snprintf(topicQuery, topicQuerySize, KTopicQuery, topic);
    if (cx < 0) {
        return -1;
    }*/

    //char topicIdStr[10];
    //itoa(topicId, topicIdStr, 10);

    //sprintf(topicIdStr,"%d",value)

    char escapedValue[strlen(value)*2+1];
    int len = mysql_real_escape_string(sql, escapedValue, value, strlen(value));
    if (len < 0) {
        DBG("mysql_real_escape_string failed!");
        return -1;
    }
    escapedValue[len] = '\0';

    const char* KInsertQuery = "INSERT INTO iot_topicdata (topic_id, %s) VALUES(%d, \"%s\")";
    const int KInsertQuerySize = strlen(KInsertQuery) + strlen(columnName) + 10 + strlen(escapedValue);
    char insertQuery[KInsertQuerySize];

    int cx = snprintf(insertQuery, KInsertQuerySize, KInsertQuery, columnName, topicId, escapedValue);
    if (cx < 0) {
        return -1;
    }

    //DBG("insert query: %s", insertQuery);

    int err = mysql_query(sql, insertQuery);
    if (err != 0) {
        printErr(sql);
        return -1;
    }
    int id = mysql_insert_id(sql);
    return id;
}
// Updates last_tdata_id column, return 0 if succees
int update_topic_lastid(MYSQL *sql, int topicId, int dataRowId)
{
    const char* KQueryFormat = "UPDATE iot_topic SET last_tdata_id = %d WHERE topic_id = %d";
    const int KQuerySize = strlen(KQueryFormat) + 20;
    char query[KQuerySize];

    int cx = snprintf(query, KQuerySize, KQueryFormat, dataRowId, topicId);
    if (cx < 0) {
        return -1;
    }

    int err = mysql_query(sql, query);
    if (err != 0) {
        printErr(sql);
        return -1;
    }
    return 0;
}
int sqlAddTopicValue(MYSQL *sql, const char* topic, const char* value) {

    int topicId = getTopicIdFromCache(topic);
    if (topicId < 0) {
        topicId = getTopicIdFromSql(sql, topic);
        if (topicId == -1) {
            // topic not found, create new one
            // create topic and try again
            topicId = sqlCreateNewTopic(sql, topic);
            DBG("created new topic: %s, topicId: %d", topic, topicId);
        }
        if (topicId > 0) {
            addTopicToCache(topic, topicId);
        }
    }
    //DBG("sqlAddTopicValue, topic: %s, id: %d", topic, topicId);

    int rowid = sql_insert_topicdata(sql, topicId, value);
    if (rowid > 0)
    {
        update_topic_lastid(sql, topicId, rowid);
    }
}

void delivered(void* context, MQTTClient_deliveryToken dt)
{
    UNUSED(context);
    UNUSED(dt);
    //printf("Message with token value %d delivery confirmed\n", dt);
    //deliveredtoken = dt;
}

int msgarrvd(void* context, char *topicName, int topicLen, MQTTClient_message *message)
{
    UNUSED(context);
    UNUSED(topicLen);
    // ignore retained messages
    if (message->retained == 0)
    {
        //printf("Message arrived\n");
        //printf("     topic: %s\n", topicName);

        char* pos = strchr(topicName, '$');

        bool ignore = (pos != NULL);

        if (!ignore)
        {
            if (message->payloadlen > 512)
            {
                sendFile(topicName, message->payload, message->payloadlen);
            }
            else
            {
                char buffer[message->payloadlen + 1];
                memcpy( &buffer, message->payload, message->payloadlen);
                buffer[message->payloadlen] = '\0';

                MYSQL* sql = SqlConnect();
                sqlAddTopicValue(sql, topicName, buffer);
                mysql_close(sql);
            }
        }
    }

    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

void connlost(void* context, char *cause)
{
    UNUSED(context);
    if (cause) {
        DBG("Connection lost, cause: %s", cause);
    }
    else {
        DBG("Connection lost");
    }
}

void finish_with_error(MYSQL *con)
{
    DBG("SQL error: %s", mysql_error(con));
    mysql_close(con);
    exit(1);
}

MYSQL* SqlConnect() {
    MYSQL *mysql = mysql_init(NULL);
    if (mysql == NULL)
    {
        DBG("mysql_init failed: %s", mysql_error(mysql));
        exit(1);
    }

    if (mysql_real_connect(mysql, config.dbHost, config.dbUsername, config.dbPassword,
        config.dbName, config.dbPort, NULL, 0) == NULL)
    {
        DBG("mysql connect failed: %s", mysql_error(mysql));
        mysql_close(mysql);
        exit(1);
    }
    mysql_set_character_set(mysql, "utf8");
    return mysql;
}

void run()
{
    curl_global_init(CURL_GLOBAL_ALL);

    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;

    MQTTClient_create(&client, config.mqttHost, config.clientId,
      MQTTCLIENT_PERSISTENCE_NONE, NULL);

    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.username = config.mqttUsername;
    conn_opts.password = config.mqttPassword;

    MQTTClient_setCallbacks(client, NULL, connlost, msgarrvd, delivered);

    /*rc = MQTTClient_connect(client, &conn_opts);
    if (rc != MQTTCLIENT_SUCCESS)
    {
      //printf("Failed to connect, return code %d\n", rc);
        DBG("MQTTClient_connect: Failed to connect\n");
        exit(-1);
    }
    else {
        DBG("MQTTClient_connect: Succeeded\n");
    }*/
    //printf("Subscribing to topic %s\nfor client %s using QoS%d\n\n", TOPIC, CLIENTID, QOS);

    int wasConnected = 0;
    int connectionErrPrinted = 0;
    do
    {
        if (!MQTTClient_isConnected(client)) {
            if (wasConnected) {
                DBG("Disconnected");
            }
            wasConnected = 0;
            rc = MQTTClient_connect(client, &conn_opts);
            if (rc == MQTTCLIENT_SUCCESS)
            {
                DBG("MQTTClient_connect: Succeeded");
                MQTTClient_subscribe(client, config.topic, QOS);
                wasConnected = 1;
                connectionErrPrinted = 0;
            }
            else if (!connectionErrPrinted) {
                DBG("MQTTClient_connect: Failed to connect");
                connectionErrPrinted = 1;
            }
        }

        sleep(1);
    } while(running);

    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    curl_global_cleanup();
    DBG("MQTTClient terminated");
}
/**
 * \brief Print help for this application
 */
void print_help(void)
{
	printf("\n Usage: %s [OPTIONS]\n\n", app_name);
	printf("  Options:\n");
	printf("   -h --help                 Print this help\n");
	printf("   -c --conf_file filename   Read configuration from the file\n");
	printf("   -t --test_conf filename   Test configuration file\n");
	printf("   -l --log_file  filename   Write logs to the file\n");
	printf("   -d --daemon               Daemonize this application\n");
	printf("   -p --pid_file  filename   PID file used by daemonized app\n");
	printf("\n");
}

/* Main function */
int main(int argc, char *argv[])
{
	static struct option long_options[] = {
		{"conf_file", required_argument, 0, 'c'},
		{"test_conf", required_argument, 0, 't'},
		{"log_file", required_argument, 0, 'l'},
		{"help", no_argument, 0, 'h'},
		{"daemon", no_argument, 0, 'd'},
		{"pid_file", required_argument, 0, 'p'},
		{NULL, 0, 0, 0}
	};
	int value;
    int option_index = 0;
	int start_daemonized = 0;

	app_name = argv[0];

	/* Try to process all command line arguments */
	while ((value = getopt_long(argc, argv, "c:l:t:p:dh", long_options, &option_index)) != -1) {
		switch (value) {
			case 'c':
				conf_file_name = strdup(optarg);
				break;
			case 'l':
				log_file_name = strdup(optarg);
				break;
			case 'p':
				pid_file_name = strdup(optarg);
				break;
			case 't':
				return test_conf_file(optarg);
			case 'd':
				start_daemonized = 1;
				break;
			case 'h':
				print_help();
				return EXIT_SUCCESS;
			case '?':
				print_help();
				return EXIT_FAILURE;
			default:
				break;
		}
	}

	/* When daemonizing is requested at command line. */
	if (start_daemonized == 1) {
		/* It is also possible to use glibc function deamon()
		 * at this point, but it is useful to customize your daemon. */
		daemonize();
	}

	/* Open system log and write message to it */
	//openlog(argv[0], LOG_PID|LOG_CONS, LOG_DAEMON);
	//syslog(LOG_INFO, "Started %s", app_name);

	/* Daemon will handle two signals */
	signal(SIGINT, handle_signal);
	signal(SIGHUP, handle_signal);



	/* Read configuration from config file */
	int ret = read_conf_file(0);

    if (ret == 0) {
    	/* This global variable can be changed in function handling signal */
    	running = 1;

        if (log_file_name) {
            // Test log file writing
            FILE* file = fopen(log_file_name, "a+");
            if (!file) {
                // Failed to open log file
                char* filename = log_file_name;
                log_file_name = NULL;
                DBG("Failed to open logfile %s", filename);
                free(filename);
            }
            else
                fclose(file);
        }

        DBG("Started.");
        run();
    }
    else {
        DBG("Failed to parse config file!");
    }

    DBG("Stopped.");

	/* Write system log and close it. */
	//syslog(LOG_INFO, "Stopped %s", app_name);
	//closelog();

	/* Free allocated memory */
    resetTopicCache();
	if (conf_file_name != NULL) free(conf_file_name);
	if (log_file_name != NULL) free(log_file_name);
	if (pid_file_name != NULL) free(pid_file_name);

	return EXIT_SUCCESS;
}
