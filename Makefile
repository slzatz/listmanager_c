listmanager_term: listmanager_term.c
	$(CC) listmanager_term.c -I . -L . -I/usr/include/python3.7m -L/usr/include/python3.7m -o listmanager_term -lpq -liniparser -lpython3.7m -Wall -Wextra -pedantic -std=c99

