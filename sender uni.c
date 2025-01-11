
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>

#define BUFFER_SIZE 1024
#define WINDOW_SIZE 10
#define TIMEOUT_MS 1500 // Увеличен таймаут до 1.5 секунд

typedef struct {
    int seq_num;
    char data[BUFFER_SIZE];
    int acked;
} packet_t;

typedef struct {
    struct timeval start_time;
    int active;
} timer_t;

packet_t window[WINDOW_SIZE];
timer_t timers[WINDOW_SIZE];
int window_start = 0;
int window_end = 0;

int sockfd;
struct sockaddr_in receiver_addr;
socklen_t addr_len = sizeof(receiver_addr);

void send_packet(int seq_num);
void start_timer(int seq_num);
void stop_timer(int seq_num);
void check_timers();
void handle_ack(int ack_num);
void send_file(const char* input_file);

// Функция отправки пакета
void send_packet(int seq_num) {
    char buffer[BUFFER_SIZE + 20]; // Дополнительное место для номера последовательности.
    snprintf(buffer, sizeof(buffer), "%d %s", seq_num, window[seq_num % WINDOW_SIZE].data);
    if (sendto(sockfd, buffer, strlen(buffer), 0, (struct sockaddr*)&receiver_addr, addr_len) >= 0) {
        printf("Отправлен пакет %d\n", seq_num);
    } else {
        perror("Ошибка отправки пакета");
    }
}

// Функция запуска таймера
void start_timer(int seq_num) {
    int index = seq_num % WINDOW_SIZE;
    gettimeofday(&timers[index].start_time, NULL);
    timers[index].active = 1;
    printf("Таймер для пакета %d запущен.\n", seq_num);
}

// Функция остановки таймера
void stop_timer(int seq_num) {
    int index = seq_num % WINDOW_SIZE;
    timers[index].active = 0;
    printf("Таймер для пакета %d остановлен.\n", seq_num);
}

// Функция проверки таймеров
void check_timers() {
    struct timeval current_time;
    gettimeofday(&current_time, NULL);

    for (int i = window_start; i < window_end; i++) {
        int index = i % WINDOW_SIZE;
        if (timers[index].active && !window[index].acked) {
            long elapsed_time = (current_time.tv_sec - timers[index].start_time.tv_sec) * 1000 +
                                (current_time.tv_usec - timers[index].start_time.tv_usec) / 1000;
            if (elapsed_time >= TIMEOUT_MS) {
                printf("Таймаут для пакета %d. Повторная отправка...\n", i);
                send_packet(i);
                start_timer(i);
            }
        }
    }
}

// Функция обработки подтверждения (ACK)
void handle_ack(int ack_num) {
    if (ack_num >= window_start && ack_num < window_end) {
        int index = ack_num % WINDOW_SIZE;
        if (!window[index].acked) {
            window[index].acked = 1;
            stop_timer(ack_num);
            printf("Получен ACK для пакета %d\n", ack_num);

            while (window_start < window_end && window[window_start % WINDOW_SIZE].acked) {
                printf("Сдвиг окна: пакет %d подтверждён.\n", window_start);
                window_start++;
            }
        }
    }
}

// Функция отправки файла
void send_file(const char* input_file) {
    FILE* file = fopen(input_file, "r");
    if (!file) {
        perror("Ошибка при открытии файла");
        exit(EXIT_FAILURE);
    }

    char line[BUFFER_SIZE];
    int seq_num = 0;

    while (fgets(line, sizeof(line), file)) {
        while (seq_num >= window_start + WINDOW_SIZE) {
            check_timers();
        }

        strncpy(window[seq_num % WINDOW_SIZE].data, line, BUFFER_SIZE);
        window[seq_num % WINDOW_SIZE].acked = 0;
        send_packet(seq_num);
        start_timer(seq_num);
        seq_num++;

        check_timers();
    }

    fclose(file);
    printf("Передача файла завершена.\n");

    char end_message[] = "END";
    sendto(sockfd, end_message, strlen(end_message), 0, (struct sockaddr*)&receiver_addr, addr_len);
    printf("Сигнал завершения передачи отправлен.\n");

    while (window_start < seq_num) {
        check_timers();
    }
}

// Главная функция
int main(int argc, char* argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <input_file> <ip> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char* input_file = argv[1];
    const char* ip = argv[2];
    int port = atoi(argv[3]);

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Ошибка при создании сокета");
        exit(EXIT_FAILURE);
    }

    memset(&receiver_addr, 0, sizeof(receiver_addr));
    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &receiver_addr.sin_addr) <= 0) {
        perror("Ошибка при преобразовании IP-адреса");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Отправка HELLO и ожидание ACK_HELLO
    char hello_message[] = "HELLO";
    sendto(sockfd, hello_message, strlen(hello_message), 0, (struct sockaddr*)&receiver_addr, addr_len);
    printf("Отправлено HELLO, ожидаем ACK_HELLO...\n");

    char buffer[BUFFER_SIZE];
    int len = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0, NULL, NULL);
    if (len > 0) {
        buffer[len] = '\0';
        if (strcmp(buffer, "ACK_HELLO") == 0) {
            printf("Получен ACK_HELLO. Начинаем отправку файла...\n");
        } else {
            fprintf(stderr, "Ошибка: ожидался ACK_HELLO, получено: %s\n", buffer);
            close(sockfd);
            exit(EXIT_FAILURE);
        }
    }

    send_file(input_file);

    // Отправка CLOSE и ожидание ACK_CLOSE
    char close_message[] = "CLOSE";
    sendto(sockfd, close_message, strlen(close_message), 0, (struct sockaddr*)&receiver_addr, addr_len);
    printf("Отправлено CLOSE, ожидаем ACK_CLOSE...\n");

    len = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0, NULL, NULL);
    if (len > 0) {
        buffer[len] = '\0';
        if (strcmp(buffer, "ACK_CLOSE") == 0) {
            printf("Получен ACK_CLOSE. Передача завершена.\n");
        }
    }

    close(sockfd);
    return 0;
}