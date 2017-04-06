#include "sh61.h"
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define F_AND 1
#define F_OR 2
#define PIPED 1

// Global to signify SIGINT
sig_atomic_t skip = 0;

// struct command
//    Data structure describing a command. Add your own stuff.

typedef struct command command;
struct command { 

    int argc;         // Number of arguments				
    char** argv;      // arguments, terminated by NULL
    pid_t pid;        // process ID running this command, -1 if none
    int group;        // Helps create process groups
    int signal;         // 1 for && and 2 for ||; 3 if in the background
    int redirection_out;    // 1 if redirecting out
    int redirection_in;     // 1 if redirecting in
    int redirection_stderr; // 1 if redirecting error message 
    char* redirection_out_file; // Hold files for redirection
    char* redirection_in_file;
    char* redirection_stderr_file;
    int pipein;   // If piping into (|)
    int pipeout;  // If piping out
    int num_pipe; // Signals multiple pipes to run_list 
    int pipefd[2];//Holds file descriptors for pipe
    int cd; // Signal of directory change to conditionals 
    command* prev;
    command* next;
};


// command_alloc()
//    Allocate and return a new command structure.

static command* command_alloc(void) {
    command* c = (command*) malloc(sizeof(command));
    c->argc = 0;
    c->argv = NULL;
    c->pid = -1;
    c->prev = c->next = NULL;
    c->group = 0;
    c->signal = 0;
    c->pipein = c->pipeout = 0;
    c->num_pipe = 0;
    c->cd = 0;
    c->redirection_out = c->redirection_in = 
    c->redirection_stderr = 0; 
    return c;
}


// command_free(c)
//    Free command structure `c`, including all its words.

static void command_free(command* c) {
    for (int i = 0; i != c->argc; ++i)
        free(c->argv[i]);
    free(c->argv);
    free(c);
}


// command_append_arg(c, word)
//    Add `word` as an argument to command `c`. This increments `c->argc`
//    and augments `c->argv`.

static void command_append_arg(command* c, char* word) {
    c->argv = (char**) realloc(c->argv, sizeof(char*) * (c->argc + 2));
    c->argv[c->argc] = word;
    c->argv[c->argc + 1] = NULL;
    ++c->argc;
}


// COMMAND EVALUATION

// start_command(c, pgid)
//    Start the single command indicated by `c`. Sets `c->pid` to the child
//    process running the command, and returns `c->pid`.
//
//    PART 1: Fork a child process and run the command using `execvp`.
//    PART 5: Set up a pipeline if appropriate. This may require creating a
//       new pipe (`pipe` system call), and/or replacing the child process's
//       standard input/output with parts of the pipe (`dup2` and `close`).
//       Draw pictures!
//    PART 7: Handle redirections.
//    PART 8: The child process should be in the process group `pgid`, or
//       its own process group (if `pgid == 0`). To avoid race conditions,
//       this will require TWO calls to `setpgid`.

pid_t start_command(command* c, pid_t pgid) {
    (void) pgid;
    pid_t c_pid = NULL;

    // Directory change
    if (strcmp(c->argv[0], "cd") == 0) {
        int check = chdir(c->argv[1]);
	c->cd = 1;
	// Upon failure, updates for conditional	
	if (check == -1) {
	    if (c->signal == 1)
		c->cd++;
	}
    }
    else {
        do { 
	    // Creates pipe
	    if (c->pipeout == PIPED || c->pipein == PIPED) {
                int check_pipe = pipe(c->pipefd);
	        assert(check_pipe == 0);  
	    }

	    // Forks to allow for execution
    	    pid_t child = fork();
            c_pid = child;
		
	    // Distinguishes parent from child process
            if (child == 0) {
	
		//Pipe end Setup
	        if (c->pipein) {
  	            dup2(c->prev->pipefd[0], STDIN_FILENO);
	        }    	       
	        else {
		    dup2(STDIN_FILENO, STDIN_FILENO);
		} 
		if (c->pipeout) {
	            dup2(c->pipefd[1], STDOUT_FILENO);         
	        } 
    	        if (c->pipeout == PIPED || c->pipein == PIPED) {
		    close(c->pipefd[0]);
		}
	

		// redirections
                int redirect_in_fd = 0;
	        int redirect_out_fd = 0;
	        int redirect_stderr_fd = 0;

		
                if (c->redirection_in == 1) {
		    // Opens file for redirection
	            redirect_in_fd = open(c->redirection_in_file, O_RDONLY);
	            if (redirect_in_fd == -1) {
	                fprintf(stderr, "No such file or directory\n");
	                _exit(1);
	            }
		    // Redirects Standard Input to file
	            dup2(redirect_in_fd, STDIN_FILENO);
                    close(redirect_in_fd);
	
                }
                if (c->redirection_out == 1) {
		    // Opens file for redirection
	            redirect_out_fd = open(c->redirection_out_file, O_WRONLY|O_CREAT|O_TRUNC, 0666);
	            if (redirect_out_fd == -1) {
	                fprintf(stderr, "No such file or directory\n");
	                _exit(1); 
	            }
		    // Redirects Standard Output to file
	            dup2(redirect_out_fd, STDOUT_FILENO);
                    close(redirect_out_fd);
                }
	
	        if (c->redirection_stderr == 1) {
		    // Opens file for redirection
	            redirect_stderr_fd = open(c->redirection_stderr_file, O_WRONLY|O_CREAT|O_TRUNC, 0666);
	            if (redirect_stderr_fd == -1) {
	                fprintf(stderr, "No such file or directory\n");
	                _exit(1); 
	            }
		    // Redirects Standard Error to file
	            dup2(redirect_stderr_fd, STDERR_FILENO);
                    close(redirect_stderr_fd);
	        }


	        //Executes argv from node
                execvp(c->argv[0], c->argv);
	        printf("Exec Failed: %s\n", strerror(errno));
                _exit(1);
                fprintf(stderr, "start_command not done yet\n");
            } 
            // If parent process, it closes
            else {
                close(c->pipefd[1]);	
            }
	    // Signals to account for multiple pipes
	    if (!c->next) {
	    	c->num_pipe = 1;
	        break;
            }
            c->num_pipe = 1;
            c = c->next;
        } while (c->pipein == PIPED);
        
    }
    return c_pid;
}


// run_list(c)
//    Run the command list starting at `c`.
//
//    PART 1: Start the single command `c` with `start_command`,
//        and wait for it to finish using `waitpid`.
//    The remaining parts may require that you change `struct command`
//    (e.g., to track whether a command is in the background)
//    and write code in run_list (or in helper functions!).
//    PART 2: Treat background commands differently.
//    PART 3: Introduce a loop to run all commands in the list.
//    PART 4: Change the loop to handle conditionals.
//    PART 5: Change the loop to handle pipelines. Start all processes in
//       the pipeline in parallel. The status of a pipeline is the status of
//       its LAST command.
//    PART 8: - Choose a process group for each pipeline.
//       - Call `set_foreground(pgid)` before waiting for the pipeline.
//       - Call `set_foreground(0)` once the pipeline is complete.
//       - Cancel the list when you detect interruption.

void run_list(command* c) {
    int stat;
    int group = 1;
    int pid = getpid();

    while (c) {

	// Accounts for extra node at end of linked list
        if (c->argc == 0) {
	    return;
	}
	
	// Groups together command group
	group = c->group;
	pid_t grouping = fork();

	// Distinguishes parents and child
	if (grouping == 0) {

	    //sets pgid and brings child to foreground
	    setpgid(0,0);
	    set_foreground(0);

	    while (c && (c->group == group)) {

	        if (c->argc == 0) {
	    	    return;
		}

		// Runs command
        	pid_t compid = start_command(c, 0); 
		waitpid(compid, &stat, 0);


	        // Check for conditional
	    	if(c->signal != 0) {
		    // Notes Directory change
		    if (c->cd != 1) {
			if (c->cd > 1) {
			    c = c->next;			
			}
		        // Checks if for exit status
	                if (WIFEXITED(stat)) {
	                    stat = WEXITSTATUS(stat);
	                }
		        if (c->signal == F_AND && stat != 0) {
	  	            while (c->signal == F_AND) {
 		                c = c->next;
		            }
		        }
		        if (c->signal == F_OR && stat == 0) {
 		            while (c->signal == F_OR) {
 		                c = c->next;
		            }
		        }
	            }
		}
                //Checks for multiple pipes
		if (c->num_pipe != 0) { 
		         
	    	    while (c) {
	   	        if (c->num_pipe != 0) {
		    	    if(c->signal != 0) {
			        // Checks if for exit status
	        		if (WIFEXITED(stat)) {
	            	    	    stat = WEXITSTATUS(stat);
				   
	                 	}
				if (c->signal == F_AND && stat != 0) {
 		    	    	    c = c->next;
				}
				if (c->signal == F_OR && stat == 0) {
				 
 		    	    	    c = c->next;
		        	}
	            	    }	      
		    	    c = c->next;
			}
			else {
		   	    break;		
			}
	    	    }
		}
		else {
	    	    c = c->next;
		}
    	    }
	    _exit(0);
	}
	
	setpgid(grouping,grouping);

	// Moves to next command group
	while(c->group == c->next->group) {
	    c = c->next;
	}
	
	// Sets child to foreground in parent
	set_foreground(grouping);

	if (c->signal != 3) {
	    waitpid(grouping, &stat, 0);
    	}

      	// Sets parent as foreground
	set_foreground(pid);
	if (skip == 1) {
	    break;	
	}

	c = c->next;
    }
}

// Sets pointers for the linked list
command* llhelper(command* h) {
    //Allocates new node
    h->next = command_alloc();
    h->next->prev = h;
    h->next->group = h->group;
    return h;
}

// eval_line(c)
//    Parse the command list in `s` and run it via `run_list`.

void eval_line(const char* s) {
    int type;
    char* token;

    // build the command
    command* com_head = command_alloc();
    command* com_cur = com_head;
    com_cur->group = 1;

    // parses the command line and creates a linked list 
    while ((s = parse_shell_token(s, &type, &token)) != NULL) {

	if (type == TOKEN_BACKGROUND) {
	    com_cur->signal = 3;	
	    llhelper(com_cur);
	    com_cur->next->group = com_cur->group + 1;
	    com_cur = com_cur->next;
	}

	else if (type == TOKEN_SEQUENCE) {
	    //Adjusts for case of ';'
	    llhelper(com_cur);
	    // Creates new group and accounts for directory changes
	    if (strcmp(com_cur->argv[0], "cd") != 0) {
	        com_cur->next->group = com_cur->group + 1;
	    }
	    com_cur = com_cur->next;
	}

	else if (type == TOKEN_AND) {
	    com_cur->signal = F_AND;
	    com_cur = llhelper(com_cur);
	    com_cur = com_cur->next;
	}

	else if (type == TOKEN_OR) {
	    com_cur->signal = F_OR;
	    com_cur = llhelper(com_cur);
	    com_cur = com_cur->next;
	}

	else if (type == TOKEN_PIPE) {
	    // Signals to Pipe
	    com_cur->pipeout = PIPED;
	    com_cur = llhelper(com_cur);
	    com_cur = com_cur->next;
	    com_cur->pipein = PIPED;	
	}

	else if (type == TOKEN_REDIRECTION) {
	    // Signal different redirections
	    if (strcmp(token,"<") == 0) {
		com_cur->redirection_in = 1;
	    }
	    else if (strcmp(token,">") == 0) {
		com_cur->redirection_out = 1;
	    }
	    else if (strcmp(token,"2>") == 0) {
		com_cur->redirection_stderr = 1;
	    }
	}

 	else if (type == TOKEN_NORMAL) {
	    // Store files for Redirection
	    if (com_cur->redirection_out == 1 && com_cur->redirection_out_file == NULL) {
                com_cur->redirection_out_file = token;
                strcpy(com_cur->redirection_out_file,token);
            }
	    else if(com_cur->redirection_stderr == 1 && com_cur->redirection_stderr_file == NULL) {
                com_cur->redirection_stderr_file = token;
                strcpy(com_cur->redirection_stderr_file,token);
            }
            else if(com_cur->redirection_in == 1 && com_cur->redirection_in_file == NULL) {
                com_cur->redirection_in_file = token;
                strcpy(com_cur->redirection_in_file,token);
            }
	    else {
	       command_append_arg(com_cur, token);
	    }
	}
    }

    if (com_cur->signal == 0) {
	llhelper(com_cur);
	com_cur->next->group = com_cur->group + 1;
        com_cur = com_cur->next;
    }

    // execute it
    if (com_head->argc) { //is this check right?
        run_list(com_head);
	command* top = com_head;

	// loop through freeing each        
	while (top) {
   	    command_free(top);
	    top = top->next;
	}
    }
}

void signal_handler() {
    skip = 1;
}

int main(int argc, char* argv[]) {
    FILE* command_file = stdin;
    int quiet = 0;

    // Check for '-q' option: be quiet (print no prompts)
    if (argc > 1 && strcmp(argv[1], "-q") == 0) {
        quiet = 1;
        --argc, ++argv;
    }

    // Check for filename option: read commands from file
    if (argc > 1) {
        command_file = fopen(argv[1], "rb");
        if (!command_file) {
            perror(argv[1]);
            _exit(1);
        }
    }

    // - Put the shell into the foreground
    // - Ignore the SIGTTOU signal, which is sent when the shell is put back
    //   into the foreground
    set_foreground(0);
    handle_signal(SIGTTOU, SIG_IGN);
     
    // Calls signal_handler for SIGINT
    handle_signal(SIGINT, signal_handler);    

    char buf[BUFSIZ];
    int bufpos = 0;
    int needprompt = 1;

    while (!feof(command_file)) {
        // Print the prompt at the beginning of the line
        if (needprompt && !quiet) {
            printf("sh61[%d]$ ", getpid());
            fflush(stdout);
            needprompt = 0;
        }

        // Read a string, checking for error or EOF
        if (fgets(&buf[bufpos], BUFSIZ - bufpos, command_file) == NULL) {
            if (ferror(command_file) && errno == EINTR) {
                // ignore EINTR errors
                clearerr(command_file);
                buf[bufpos] = 0;
            } else {
                if (ferror(command_file))
                    perror("sh61");
                break;
            }
        }
	// In case of SIGINT prints new prompt
	if (skip == 1) {
	    bufpos = 0;
	    needprompt = 1;
	    skip = 0;
	    printf("\n");
	    continue;	
	}
        // If a complete command line has been provided, run it
        bufpos = strlen(buf);
        if (bufpos == BUFSIZ - 1 || (bufpos > 0 && buf[bufpos - 1] == '\n')) {
            eval_line(buf);
            bufpos = 0;
            needprompt = 1;
        }

        // Handle zombie processes and/or interrupt requests
	int r;
        while ((r = waitpid(-1, 0, WNOHANG)) != -1 && r != 0) {}
    }

    return 0;
}
