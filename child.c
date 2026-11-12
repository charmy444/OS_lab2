#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;
    size_t number = 0;
    int result = 0;

    while ((length = getline(&line, &capacity, stdin)) != -1) {
        number++;
        ssize_t end = length;

        if (end > 0 && line[end - 1] == '\n')
            end--;
        if (end > 0 && line[end - 1] == '\r')
            end--;

        if (end > 0 && (line[end - 1] == '.' || line[end - 1] == ';')) {
            // stdout уже перенаправлен родителем в файл
            if (fwrite(line, 1, (size_t)length, stdout) != (size_t)length ||
                fflush(stdout) == EOF) {
                perror("write file");
                result = 1;
                break;
            }
            // служебное подтверждение, одна пустая строка
            if (fputc('\n', stderr) == EOF) {
                result = 1;
                break;
            }
        } else {
            if (fprintf(stderr,
                        "ошибка - строка %zu должна оканчиваться на '.' или ';'\n",
                        number) < 0) {
                result = 1;
                break;
            }
        }
        if (fflush(stderr) == EOF) {
            result = 1;
            break;
        }
    }

    if (length == -1 && !feof(stdin)) {
        perror("read stdin");
        result = 1;
    }
    free(line);
    if (fclose(stdout) == EOF) {
        perror("close file");
        result = 1;
    }
    return result;
}
