/** 
 * @author Miguel Joshua Vallespin Mendoza
 * UID 3035800693
 * Development Platform: workbench2 @ Visual Studio Code v1.83.1
 * 
 * How Much Done:
 *      Should be able to print shell prompt and user's input
 *      Should be able to execute command enetered by the user
 *      Can locate and execute a command with a full path or with a
 *      relative path
 *      Can execute a command with any number of arguments
 *      Can handle error situations correctly 
 *      Should wait for the command to complete before accepting the next command
 *      Should accept next command for execution
 *      Should remove all zombie processes
 *      The system can print the process’s running statistics in theccorrect format
 *      The system should print the information after the command/job completed
 *      The program should retrieve the data from the /proc filesystem (if not, -1 point).
 *      Correct bahavior of JCshell process and child processes in handling SIGINT
 *      All child processes should wait for SIGUSR1 signal before executing target commands
 *      Correct use of exit command; can report improper usage
 *      Can report improper use of | symbol
 *      
 * 
 * How much NOT Done:
 *      Incorrect use of | symbol
 *      Cannot execute multiple commands (at most five) with any number of arguments
 *      that are connected by pipes
 *      For a job with multiple commands, the system cannot output the statistics 
 *      according to the termination order
 *      Cannot locate and execute a command under the standard PATH
*/

#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>
#include <signal.h>
#include <setjmp.h>
#include <ctype.h>
#include <stdbool.h>

int maxChar = 1024;                              
int maxWords = 30;  
char dirErrorMsg[50]; 
//to pass to sigint_handler
pid_t baseShell;
//to pass to sigint_handler
pid_t childProcess; 
int shouldRun = 0;  
         

int executeCommand(char*, int);
int executePipe(char**, int);
//char* dir_exists(char*, char**);
int moveDir(char*);
void printProcessStats(pid_t, char*, int);
void sigint_handler(int);
void sigusr1_handler(int);
int countPipes(char*);

int main() {
    bool skipExec = false;
    pid_t shellPID = getpid();
    baseShell = shellPID;
    char command[maxChar];
    //start at root directory, can comment to make things easier
    chdir("/");     

    signal(SIGINT, sigint_handler);
    while (1) {
        //shell prompt
        printf( "## JCShell [%d] ## ", shellPID);
        //reads input from terminal
        fgets(command, maxChar, stdin);    
        //removes null byte at the end (i.e. \n)
        command[strlen(command)-1] = '\0'; 

        int pipeCount = countPipes(command);

        //check if the command starts or ends with a pipe
        if (command[0] == '|' || command[strlen(command)-1] == '|') {
            printf("JCShell: command should not start or end with |\n");
            continue;
        }

        if (pipeCount > 0) {
            char* pipeCommands[pipeCount + 1];
            int i = 0;
            int start = 0;  //start index of a subcommand
            for (int end = 0; end < strlen(command); end++) {
                if (command[end] == '|') {
                    //skip spaces after the '|'
                    int next = end + 1;
                    while (next < strlen(command) && command[next] == ' ') next++;
                    //if next non-space character is another '|', print an error message
                    if (next < strlen(command) && command[next] == '|') {
                        printf("JCShell: should not have two | symbols without an in-between command\n");
                        skipExec = true;
                        break;
                    }
                    // replace '|' with '\0'
                    command[end] = '\0';  
                    pipeCommands[i++] = &command[start];
                    start = end + 1;
                }
            }
            if (!skipExec) {
                //add the last subcommand
                pipeCommands[i] = &command[start];  
                //for handling pipe commands
                executePipe(pipeCommands, pipeCount + 1);
            }
        } else {
            char* commandCopy = strdup(command);
            char* token;
            char* argv[maxWords];
            int argc = 0;
            token = strtok(command, " ");
            while (token != NULL && argc < maxWords) {
                argv[argc++] = token;
                token  = strtok(NULL, " ");
            }
            argv[argc] = NULL;

            //checks if "exit" has any other arguments
            if (strcmp(argv[0], "exit") == 0) {
                if (argv[1] != NULL) {
                    printf("JCShell: \"exit\" with other arguments!!!\n");
                }
                else {
                    printf("JCShell: Terminated\n");
                    exit(1);
                }
            } else {
                executeCommand(commandCopy, maxWords);
                free(commandCopy);
            }
        }   
    }
    return 0;
}

int executeCommand(char* command, int maxWords) {
    //for tokenizing commands
    char *token;            
    char *argv[maxWords];
    int argc = 0;
    pid_t childPID;

    token = strtok(command, " ");
    //char* dirPath = dir_exists(command, argv);

    //tokenizing
    while (token != NULL && argc < maxWords) {      
        argv[argc++] = token;
        token  = strtok(NULL, " ");
    }
    argv[argc] = NULL;

    //absolute path traversal
    if(argv[0][0] == '/') {     
    
        if(chdir(argv[0]) < 0){
            snprintf(dirErrorMsg, 50, "JCShell: %s", command); 
            perror(dirErrorMsg);

            return 1;
        } else {
            printf("Changed to %s\n", getcwd(NULL,0));
            return 0;
        }
    //relative path downwards traversal OR executable
    } else if(argv[0][0] == '.')  { 
        //to check if file is an executable or a directory
        struct stat fileStat;   
        if (stat(argv[0], &fileStat) == 0 && S_ISREG(fileStat.st_mode) 
            && fileStat.st_mode & S_IXUSR) {
                
            childPID = fork();  

            if (childPID == 0) {;
                signal(SIGUSR1, sigusr1_handler);
                //call kill -SIGUSR1 $(cat pid_file.txt) in another terminal to allow execution
                //the kill command sends SIGUSR1, which the JCShell uses as a control
                //such that commands will not execute immediately after creation 
                //this implementation took inspiration from Mini Lab 2 
                //FILE *fp = fopen("pid_file.txt", "w"); 
                //if (fp != NULL) {
                //    fprintf(fp, "%d", getpid()); 
                //    fclose(fp); 
                //} else {
                //    perror("fopen");
                //    exit(EXIT_FAILURE);
                //}
                //if manual input is needed for the control mechanism, 
                //uncomment code above

                //wait until shouldRun is not 0
                while (!shouldRun) { 
                    pause();
                }
                if (shouldRun) {  
                    execvp( argv[0], argv);
                    perror("execvp");
                    exit(EXIT_FAILURE);
                } else {
                    printf("SIGUSR1 was not received, exiting...\n");
                    exit(EXIT_FAILURE);
                }
            } else if (childPID < 0) {
                perror("fork");
                exit(EXIT_FAILURE);
            } else {
                signal(SIGUSR1, sigusr1_handler);
                childProcess = childPID;
                siginfo_t processInfo;
                int status;
                //prevents race condition
                usleep(1000);
                kill(childPID, SIGUSR1);

                waitpid(processInfo.si_pid, &status, 0);
                char cmd[100];
                strcpy(cmd, &argv[0][2]); 
                printProcessStats(childPID, cmd, status); 
            }
        } else {

            return moveDir(command);
        }
    
    }  else if(argv[0][0] == '.' && argv[0][1] == '.')  { 
          
        return moveDir(command);
    } else {
        //fork child process
        childPID = fork();  

        if (childPID == 0) {
            signal(SIGUSR1, sigusr1_handler);
            //call kill -SIGUSR1 $(cat pid_file.txt) in another terminal to allow execution
            //the kill command sends SIGUSR1, which the JCShell uses as a control
            //such that commands will not execute immediately after creation 
            //this implementation took inspiration from Mini Lab 2 
            //FILE *fp = fopen("pid_file.txt", "w"); 
            //if (fp != NULL) {
            //    fprintf(fp, "%d", getpid()); 
            //    fclose(fp); 
            //} else {
            //    perror("fopen");
            //    exit(EXIT_FAILURE);
            //}
            //if manual input is needed for control mechanism,
            //uncomment code above

            //wait here until shouldRun is not 0
            while (!shouldRun) { 
                pause();
            } 
            if (shouldRun) { 
                execvp( argv[0], argv);
                perror("execvp");
                exit(EXIT_FAILURE);
            } else {
                printf("SIGUSR1 was not received, exiting...\n");
                exit(EXIT_FAILURE);
            }
        } else if (childPID < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        } else {
            signal(SIGUSR1, sigusr1_handler);
            childProcess = childPID;
            siginfo_t processInfo;
            int status;

            //prevents race condition
            usleep(1000);
            kill(childPID, SIGUSR1);

            waitpid(processInfo.si_pid, &status, 0);
            printProcessStats(childPID, command, status);  
        }

        printf( "\nCommand '%s' completed with PID %d\n", command, childPID);
        return EXIT_SUCCESS;
    }
}

int executePipe(char *pipeCommands[], int pipeCount) {
    
    int pipeFDS[pipeCount][2];
    pid_t childPID;
    pid_t childList[pipeCount + 1];

    for (int i = 0; i < pipeCount; i++) {
        if (pipe(pipeFDS[i]) < 0) {
            perror("Couldn't Pipe");
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i <= pipeCount; i++) {
            int status;
            childPID = fork();

            if (childPID == 0) {
                // Child process
                printf("Now handling %s\n", pipeCommands[i]);

                // Connect input to the appropriate pipe
                if (i != 0) {
                    if (dup2(pipeFDS[i - 1][0], STDIN_FILENO) == -1) {
                        perror("dup2");
                        exit(EXIT_FAILURE);
                    }
                    close(pipeFDS[i - 1][0]); // close read end in the child after dup2
                    close(pipeFDS[i - 1][1]);
                }

                // Connect output to the appropriate pipe (except for the last command)
                if (i != pipeCount) {
                    if (dup2(pipeFDS[i][1], STDOUT_FILENO) == -1) {
                        perror("dup2");
                        exit(EXIT_FAILURE);
                    }
                    close(pipeFDS[i][0]); // close read end in the child after dup2
                    close(pipeFDS[i][1]);
                }

                // Tokenize the command and execute it
                char commandCopy[maxChar];
                strcpy(commandCopy, pipeCommands[i]);

                // Tokenize the command and execute it
                char* token;
                char* argv[maxWords];
                int argc = 0;

                token = strtok(commandCopy, " "); // Tokenize command into arguments

                while (token != NULL && argc < maxWords) {
                    argv[argc++] = token;
                    token = strtok(NULL, " ");
                }
                argv[argc] = NULL;

                execvp(argv[0], argv);
                perror("execvp");
                exit(EXIT_FAILURE);
            } else if (childPID < 0) {
                perror("fork");
                exit(EXIT_FAILURE);
            }       
        }

        // Close all pipe file descriptors in the parent process
        for (int i = 0; i < pipeCount; i++) {
            close(pipeFDS[i][0]);
            close(pipeFDS[i][1]);
        }

        // Wait for all child processes to finish
        for (int i = 0; i <= pipeCount; i++) {
            wait(NULL);
        }
        return EXIT_SUCCESS;
}





int moveDir(char* command) {
    if (chdir(command) < 0) {
        char dirErrorMsg[50];
        snprintf(dirErrorMsg, 50, "JCShell: %s", command);
        perror(dirErrorMsg);
        return 1;
    } else {
        printf("Changed to %s\n", getcwd(NULL, 0));
        return 0;
    }
}

int countPipes(char* command) {

  int pipeCount = 0;
  char* p = strchr(command, '|');

  while(p) {
    pipeCount++;
    p = strchr(p+1, '|'); 
  }

  return pipeCount;

}

void printProcessStats(pid_t childPID, char* command, int status) {
    int foo_int, pid, ppid, excode, vctx, nvctx;
    unsigned long user, sys, foo_long;
    char foo_char, state, stat_str[50], status_str[50], cmd[50], line[maxChar];
    FILE *stat_file, *status_file;

    //waitid-example.c <-> readproc-example.c FUUUUUSION
    siginfo_t processInfo;
    struct rusage usage;
    shouldRun = 0;

    //unsure why there are error squigglies here
    //https://github.com/aiot-lab/HKU-COMP3230A-Tutorialabs/blob/main/Tutorial2-Process/2023-PA1-supplementary/waitid-example.c
    int ret = waitid(P_ALL, 0, &processInfo, WNOWAIT | WEXITED);
    if (!ret) {
        //(https://github.com/aiot-lab/HKU-COMP3230A-Tutorialabs/blob/main/Tutorial2-Process/2023-PA1-supplementary/readproc-example.c)
        //opening /proc/{pid}/stat file
        sprintf(stat_str, "/proc/%d/stat", processInfo.si_pid);          
        stat_file = fopen(stat_str, "r");
        if (stat_file == NULL) {
            printf("ERROR in opening /proc/%d/stat file\n", childPID);
            return;
        }
        //scanning /proc/{pid}/stat file
        fscanf(stat_file, "%d %s %c %d %d %d %d %d %u %lu %lu %lu %lu %lu %lu",
                &pid, &foo_char, &state, &ppid, &foo_int, &foo_int, &foo_int, &foo_int,
                (unsigned*) &foo_int, &foo_long, &foo_long, &foo_long, &foo_long,
                &user, &sys);
        fclose(stat_file);
        //opening /proc/{pid}/status file
        sprintf(status_str, "/proc/%d/status", processInfo.si_pid);          
        status_file = fopen(status_str, "r");
        if (status_file == NULL) {
            printf("ERROR in opening /proc/%d/status file\n", childPID);
            return;
        }
        //scanning /proc{pid}/status file
        while (fgets(line, sizeof(line), status_file) != NULL) {
            //read second last line 
            sscanf(line, "voluntary_ctxt_switches: %d", &vctx);
            //read last line
            sscanf(line, "nonvoluntary_ctxt_switches: %d", &nvctx);
        }


        waitpid(processInfo.si_pid, &status, 0);
        //if normal exit
        if (WIFEXITED(status)) {
            printf("\n(PID)%d (CMD)%s (STATE)%c (EXCODE)%d (PPID)%d (USER)%.2ld (SYS)%.2ld (VCTX)%d (NVCTX)%d\n", 
                    pid, command, state, WEXITSTATUS(status), ppid, user, sys, vctx, nvctx);
        //if signal exit
        } else if (WIFSIGNALED(status)){
            int signum = WTERMSIG(status);
            printf("\n(PID)%d (CMD)%s (STATE)%c (EXSIG)%s (PPID)%d (USER)%.2ld (SYS)%.2ld (VCTX)%d (NVCTX)%d\n", 
                    pid, command, state, sys_siglist[signum], ppid, user, sys, vctx, nvctx);
        }
    } else {
        perror("waitid");
    } 
    childProcess = 0;
    return;
}

void sigint_handler(int signum) {
    if (childProcess == 0) {
    printf("\n## JCShell [%d] ## ", baseShell);
    fflush(stdout);
    } else {
        return;
    }
}

void sigusr1_handler(int signum) {
    sleep(1);
    shouldRun = 1;
}
//char* dir_exists(char* cmd, char** tokens) {

 // char* path = getenv("PATH");
 // char* p = strtok(path, ":");

  //char* found_path = NULL;

  //while(p) {

    //char curr_path[1024];  
    //snprintf(curr_path, sizeof(curr_path), "%s/%s", p, tokens[0]);

    //if(access(curr_path, X_OK) == 0) {
    //  found_path = curr_path;
    //  break;
   // }

   //p = strtok(NULL, ":");
  //}

  //return found_path;
//}