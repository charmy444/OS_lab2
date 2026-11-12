#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int exchange(int write_fd, int read_fd)
{
    // объект, через который библиотека C будет работать с этим дескриптором (файл - тип структуры)
    FILE *to_child = fdopen(write_fd, "w");
    if (to_child == NULL) {
        perror("fdopen pipe1");
        close(write_fd);
        close(read_fd);
        return 1;
    }

    // аналогично
    FILE *from_child = fdopen(read_fd, "r");
    if (from_child == NULL) {
        perror("fdopen pipe2");
        fclose(to_child);
        close(read_fd);
        return 1;
    }

    char *line = NULL; // строка с записью
    char *reply = NULL; // ответ ребенка
    size_t line_capacity = 0, reply_capacity = 0;
    ssize_t length;
    int result = 0;

    while ((length = getline(&line, &line_capacity, stdin)) != -1) {
        // добавляем разделитель во втором условии, если последняя строка пришла без enter
        if (fwrite(line, 1, (size_t)length, to_child) != (size_t)length ||
            (line[length - 1] != '\n' && fputc('\n', to_child) == EOF) ||
            fflush(to_child) == EOF) {
            perror("write pipe1");
            result = 1;
            break;
        }

        ssize_t reply_length = getline(&reply, &reply_capacity, from_child);
        if (reply_length == -1) {
            if (feof(from_child)) // конец потока
                fprintf(stderr, "дочерняя программа завершилась без ответа\n");
            else // не конец потока
                perror("read pipe2");
            result = 1;
            break;
        }

        // пустой ответ означает успех, остальные ответы выводим пользователю
        if (reply_length > 1 &&
            (fwrite(reply, 1, (size_t)reply_length, stdout) != (size_t)reply_length ||
             fflush(stdout) == EOF)) {
            perror("write console");
            result = 1;
            break;
        }
    }

    // ошибка при чтении, символ окончания строки не обнаружен
    if (length == -1 && !feof(stdin)) {
        perror("read stdin");
        result = 1;
    }

    // закрытие pipe1 сообщает ребёнку, что новых строк больше не будет
    if (fclose(to_child) == EOF) {
        perror("close pipe1");
        result = 1;
    }

    // забираем возможные сообщения об ошибках при завершении ребёнка
    while ((length = getline(&reply, &reply_capacity, from_child)) != -1) {
        if (length > 1 &&
            fwrite(reply, 1, (size_t)length, stdout) != (size_t)length) {
            perror("write console");
            result = 1;
            break;
        }
    }

    // аналагично проверяем как с каналоми родителя
    if (length == -1 && !feof(from_child)) {
        perror("read pipe2");
        result = 1;
    }
    if (fclose(from_child) == EOF) {
        perror("close pipe2");
        result = 1;
    }
    // сбрасываем буфер так как в вайле не вызывали сброс
    if (fflush(stdout) == EOF) {
        perror("flush console");
        result = 1;
    }
    free(line);
    free(reply);
    return result;
}

int main(void)
{
    // вместо аварийного завершения получим ошибку записи
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("signal");
        return 1;
    }

    char *filename = NULL;
    size_t capacity = 0;
    ssize_t length = getline(&filename, &capacity, stdin);
    if (length == -1) {
        if (feof(stdin))
            fprintf(stderr, "не введено имя файла\n");
        else
            perror("read filename");
        free(filename);
        return 1;
    }
    if (length > 0 && filename[length - 1] == '\n')
        filename[--length] = '\0';
    if (length > 0 && filename[length - 1] == '\r')
        filename[--length] = '\0';

    int file = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    free(filename);
    if (file == -1) {
        perror("open");
        return 1;
    }

    int pipe1[2], pipe2[2];
    if (pipe(pipe1) == -1) {
        perror("pipe1");
        close(file);
        return 1;
    }
    if (pipe(pipe2) == -1) {
        perror("pipe2");
        close(file);
        close(pipe1[0]);
        close(pipe1[1]);
        return 1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        close(file);
        close(pipe1[0]);
        close(pipe1[1]);
        close(pipe2[0]);
        close(pipe2[1]);
        return 1;
    }

    if (pid == 0) {
        if (dup2(pipe2[1], STDERR_FILENO) == -1 ||
            dup2(pipe1[0], STDIN_FILENO) == -1 ||
            dup2(file, STDOUT_FILENO) == -1) {
            perror("dup2");
            _exit(1);
        }
        close(pipe1[0]);
        close(pipe1[1]);
        close(pipe2[0]);
        close(pipe2[1]);
        close(file);

        execl("./child", "child", (char *)NULL);
        perror("execl");
        _exit(1);
    }

    close(pipe1[0]);
    close(pipe2[1]);
    close(file);
    int result = exchange(pipe1[1], pipe2[0]);

    int status;
    pid_t waited;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited == -1 && errno == EINTR); // повторяем пока прерывается сигналом

    if (waited == -1) {
        perror("waitpid");
        return 1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "дочерняя программа завершилась с ошибкой\n");
        return 1;
    }
    return result;
}
