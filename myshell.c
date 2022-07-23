#include<stdlib.h>
#include<stdio.h>
#include<unistd.h>
#include<string.h>
#include<sys/wait.h>
#include<sys/types.h>
#include<readline/readline.h>
#include<readline/history.h>

#define RE_HISTORY ".myshell_history"
#define RL_BUFSIZE 1024
#define TOK_BUFSIZE 64
#define TOK_DELIM " \t\r\n\a"

// history search
typedef struct node_t node_t;
struct node_t
{
    char* command;
    node_t* prev;
    node_t* next;
};
typedef struct queue_t queue_t;
struct queue_t
{
    node_t *head;
    node_t *tail;
    node_t *cursor; // for up/down arrow search
};
queue_t* queue_new(void);
void queue_delete(void *self);
int queue_is_empty(const queue_t *self);
int queue_enqueue(queue_t *self, const char* command);
char* queue_dequeue(queue_t* slef);
node_t * node_new(const char* command);
void node_delete(node_t* self);
void queue_record_and_destory(queue_t* self, FILE* file);
void command_copy(char* dest, const char* source);
queue_t* his;
FILE* history_file=NULL;
void myshell_load_history();
// output redirection
char outFilename[RL_BUFSIZE];
FILE* outFile=NULL;
void clean_outFile();
// supported internal commands
char* internalcmd_collection[]={
    "cd",
    "pwd",
    "export",
    "echo",
    "exit",
    "history"
};
int func_cd(char** args);
int func_pwd(char** args);
int func_export(char** args);
int func_echo(char** args);
int func_exit(char **args);
int func_history(char **args);
int (*internal_command[])(char** args)={
    &func_cd,
    &func_pwd,
    &func_export,
    &func_echo,
    &func_exit,
    &func_history
};
// myshell main structure
char buf[128];
char *myshell_read_line(const char* prompt);
char **myshell_split_line(char *line);
void myshell_loop();
int myshell_execute(char** args);
int myshell_background(char** args, int pos);
int myshell_redirection(char** args, int pos);
int myshell_external(char **args);
int myshell_internalcmd_num();

int main(int argc, char **argv)
{
    // Load config files, if any.
    printf("MyShell initializing...\n");
    myshell_load_history();
    history_file=fopen(RE_HISTORY, "a+");
    if(!history_file)
    {
        printf("\033[31m[myshell]\033[0mcould't record history file!\n");
        printf("MyShell initialized \033[31mfailed\033[0m. Cannot record command history. Starting...\n");
        myshell_loop();
        exit(EXIT_FAILURE);
    }
    his=queue_new();

    // Run command loop.
    printf("MyShell initialized successfully. Starting...\n");
	myshell_loop();

    // Perform any shutdown/cleanup.
    queue_record_and_destory(his, history_file);
    fclose(history_file);
    printf("......MyShell Exited......\n");

	return EXIT_SUCCESS;
}

void myshell_loop()
{
	char *line;
	char **args;
	int status=1;
    char myshell_prompt[64];

	do {
        // fflush
        fflush(stdin);
        // prompt info
        sprintf(myshell_prompt, "\033[92m%s\033[0m:\033[94m%s\033[0m$ ", getlogin(), getcwd(buf,sizeof(buf)));
        // read next command
        line = readline(myshell_prompt);
        // record command for history search
        add_history(line);
        queue_enqueue(his, line);
        // divide a command line into arguments
        args = myshell_split_line(line);
        // execute and return status
        status = myshell_execute(args);

        free(line);
        free(args);
        
	} while (status);
}
char *myshell_read_line(const char* prompt)
{
    char *line = NULL;

    line=readline(prompt);

    return line;
}
char **myshell_split_line(char *line)
{
    int bufsize = TOK_BUFSIZE, position = 0;
    char **tokens = malloc(bufsize*sizeof(char*));
    char *token;

    if (!tokens)
    {
        fprintf(stderr, "\033[31m[myshell]\033[0mallocation error\n");
        exit(EXIT_FAILURE);
    }

    token = strtok(line, TOK_DELIM);

    while (token != NULL)
    {
        tokens[position] = token;
        position++;

        if (position >= bufsize)
        {
            bufsize += TOK_BUFSIZE;
            tokens = realloc(tokens, bufsize*sizeof(char*));
            if (!tokens)
            {
                fprintf(stderr, "\033[31m[myshell]\033[0mallocation error\n");
                exit(EXIT_FAILURE);
            }
        }

        token = strtok(NULL, TOK_DELIM);
    }

    tokens[position] = NULL;
    free(token);

    return tokens;
}
int myshell_external(char **args)
{
    pid_t pid, wpid;
    int status;

    pid = fork();
    if(pid == 0)
    {
        // Child process
        if (execvp(args[0], args) == -1)
            printf("\033[31m[myshell]\033[0mworng command: %s\n",args[0]);
        exit(EXIT_FAILURE);
    }
    else if(pid < 0)
    {
        // Error forking
        printf("\033[31m[myshell_external]\033[0mfork error\n");
    }
    else
    {
        // Parent process
        do{
            wpid = waitpid(pid, &status, WUNTRACED);
        }while(!WIFEXITED(status) && !WIFSIGNALED(status));
    }

    return 1;
}
int myshell_background(char** args, int pos)
{
    args[pos]=NULL;

    pid_t pid, wpid;
    int status;
    
    pid = fork();
    if(pid == 0)
    {
        // background process
        myshell_execute(args);
        exit(EXIT_SUCCESS);
    }
    else if(pid < 0)
    {
        // Error forking
        printf("\033[31m[myshell_background]\033[0mfork error\n");
    }

    return 1;
}
int myshell_redirection(char** args, int pos)
{
    if(args[pos+1]==NULL)
    {   
        printf("\033[31m[myshell_outputRedirection]\033[0mfiles can't be NULL\n");
        return 1;
    }
    strcpy(outFilename, args[pos+1]);
            
    if(!strcmp(args[pos], ">"))
        outFile=fopen(outFilename, "w");
    else if(!strcmp(args[pos], ">>"))
        outFile=fopen(outFilename, "a+");
    
    args[pos]=NULL;
    args[pos+1]=NULL;

    int status = myshell_execute(args);

    // clean file name and close file pointer
    clean_outFile();
            
    return status;
}
int myshell_execute(char** args)
{
    int i;

    // empty command
    if (args[0] == NULL) 
        return 1;

    // background
    for(int i=0;i<TOK_BUFSIZE;i++)
        if(args[i] && !strcmp(args[i], "&"))
            return myshell_background(args, i);

    // output redirection
    for(int i=0;i<TOK_BUFSIZE;i++)
        if(args[i] && (!strcmp(args[i], ">") || !strcmp(args[i], ">>")))
            return myshell_redirection(args, i);

    // internal commands
    for (i = 0; i < myshell_internalcmd_num(); i++)
        if(!strcmp(args[0], internalcmd_collection[i]))
            return (*internal_command[i])(args);
    
    return myshell_external(args);
}
int myshell_internalcmd_num()
{
    return sizeof(internalcmd_collection)/sizeof(char *);
}
int func_cd(char** args)
{
    if(args[1] == NULL)
        printf("\033[31m[myshell]\033[0mexpected argument to \"cd\"\n");
    else if(chdir(args[1]))
        printf("\033[31m[myshell]\033[0m\"cd\" chdir error\n");
        
    return 1;
}
int func_pwd(char** args)
{
    if(outFile)
        fprintf(outFile, "%s\n", getcwd(buf,sizeof(buf)));
    else
        printf("%s\n", getcwd(buf,sizeof(buf)));
    return 1;
}
int func_export(char** args)
{
    char *value;
    for(int i=0;i<TOK_BUFSIZE;i++)
        if(args[1][i]=='=')
        {
            args[1][i]='\0';
            value=&(args[1][i+1]);
            break;
        }

    if(!getenv(args[1]))
    {
        printf("\033[31m[myshell]\033[0mno found environment variable \"%s\"\n", args[1]);
        return 1;
    }

    setenv(args[1], value, 1);
    printf("set environment variable \"%s\":\n%s\n", args[1], getenv(args[1]));
    return 1;
}
int func_echo(char** args)
{
    FILE* temp=outFile;
    if(!outFile)
        outFile=stdout;

    int potinter=1;
    while(args[potinter])
        fprintf(outFile,"%s ", args[potinter++]);
    fprintf(outFile, "\n");
    
    outFile=temp;

    return 1;
}
int func_exit(char **args)
{
    // call exit
    return 0;
}
int func_history(char **args)
{
    args[0]="cat";
    args[1]=RE_HISTORY;
    for(int i=2;i<TOK_BUFSIZE;i++)
        args[i]=NULL;
    return myshell_external(args);
}
void clean_outFile()
{
    for(int i=0;i<RL_BUFSIZE;i++)
        outFilename[i]=0;

    if(outFile)
        fclose(outFile);
    
    outFile=NULL;
}
queue_t* queue_new(void)
{
    queue_t *q = (queue_t*)malloc(sizeof(queue_t));
    if (!q) {
        return q;
    }
    
    q->head = NULL;
    q->tail = NULL;
    
    return q;
}
void queue_delete(void *self)
{
    if (!self)
        return;
    
    node_t *curr = ((queue_t *) self)->head;
    while (curr) {
        node_t *temp = curr;
        curr = curr->next;
        node_delete(temp);
    }
    
    free(self);
}
int queue_is_empty(const queue_t *self)
{   
    return !(self->head) ? 1 : 0;
}
int queue_enqueue(queue_t *self, const char* command)
{
    if(!command || command[0]=='\n')
        return 0;
    node_t *node =node_new(command);
    if (!node)
        exit(EXIT_FAILURE);
    else
    {
        node->prev = NULL;
        node->next = NULL;
    }
    
    
    if (!(self->head))
	{
        self->head = node;
        self->tail = node;
        return 1;
    }

    self->tail->next = node;
    node->prev = self->tail;
    self->tail = node;
    
    return 1;
}
node_t * node_new(const char* command)
{
    node_t *node =(node_t*)malloc(sizeof(node_t));
    if (!node)
        return node;

    node->command=(char*)malloc(sizeof(char)*(strlen(command)+1));
    command_copy(node->command, command);
    //printf("in node_new:%s\n", node->command);
    node->prev = NULL;
    node->next = NULL;
    
    return node;
}
void node_delete(node_t* self)
{
    if(!self)
        return;
    
    free(self->command);
    free(self);
}
char* queue_dequeue(queue_t* self)
{
    char* popped =(char*)malloc(RL_BUFSIZE);
    command_copy(popped, self->head->command);
    popped[strlen(self->head->command)]='\0';
    //printf("in dq:%s\n", popped);
    if (self->head == self->tail)
	{
        node_delete(self->head);
        self->head = NULL;
        self->tail = NULL;
    }
    else
	{
        node_t *curr = self->head;
        self->head = curr->next;
        node_delete(curr);
    }
    
    return popped;
}
void queue_record_and_destory(queue_t* self, FILE* file)
{
    if(!self)
        return;
    char* command;
    while(!queue_is_empty(his))
    {
        command=queue_dequeue(his);
        //printf("in record_and_destory:%s\n", command);
        fprintf(history_file, "%s\n", command);
        //fprintf(stdout, ">> %s\n", command);
        free(command);
    }
    queue_delete(his);
}
void command_copy(char* dest, const char* source)
{
    int i;
    for(i=0;source[i]!='\0'&&source[i]!='\n';i++)
        dest[i]=source[i];
    dest[++i]='\0';
}
void myshell_load_history()
{
    history_file=fopen(RE_HISTORY, "r");
    if(!history_file)
        return;

    int count=0;
    char temp;
    while(fscanf(history_file,"%c",&temp)!=EOF)
        if(temp=='\n')
            count++;
    fclose(history_file);

    history_file=fopen(RE_HISTORY, "r");
    char line[TOK_BUFSIZE];
    for(int i=0;i<count;i++)
    {
        fgets(line, TOK_BUFSIZE, history_file);
        for(int j=0;j<TOK_BUFSIZE;j++)
            if(line[j]=='\n')
                line[j]='\0';
        add_history(line);
    }
    fclose(history_file);
}