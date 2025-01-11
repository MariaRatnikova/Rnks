#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <locale.h>

#define MAX_PACKETS 1000
#define BUFFER_SIZE 1024
#define PORT 12345

char* received_data[MAX_PACKETS] = {0};
int sockfd;

void init_socket() {
    sockfd = socket(AF_INET, SOCK_DGRAM, 0); // Изменено на AF_INET для IPv4
    if (sockfd < 0) {
        perror("Ошибка при создании сокета");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Ошибка при привязке сокета");
        exit(EXIT_FAILURE);
    }

    printf("Сокет инициализирован и готов к приёму данных на порту %d\n", PORT);
}

void connect_phase() {
    char buffer[BUFFER_SIZE];
    struct sockaddr_in from_addr;
    socklen_t addr_len = sizeof(from_addr);

    int len = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&from_addr, &addr_len);
    if (len > 0) {
        buffer[len] = '\0';
        if (strcmp(buffer, "HELLO") == 0) {
            printf("Получено HELLO. Отправляем ACK_HELLO...\n");
            sendto(sockfd, "ACK_HELLO", 9, 0, (struct sockaddr*)&from_addr, addr_len);
        }
    }
}

void disconnect_phase() {
    char buffer[BUFFER_SIZE];
    struct sockaddr_in from_addr;
    socklen_t addr_len = sizeof(from_addr);

    int len = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&from_addr, &addr_len);
    if (len > 0) {
        buffer[len] = '\0';
        if (strcmp(buffer, "CLOSE") == 0) {
            printf("Получено CLOSE. Отправляем ACK_CLOSE...\n");
            sendto(sockfd, "ACK_CLOSE", 9, 0, (struct sockaddr*)&from_addr, addr_len);
        }
    }
}

void store_packet(int seq_num, char* line) {
    if (seq_num < 0 || seq_num >= MAX_PACKETS) {
        printf("Ошибка: номер пакета %d вне допустимого диапазона.\n", seq_num);
        return;
    }

    if (received_data[seq_num] != NULL) {
        printf("Пакет с номером %d уже принят, игнорируем повтор.\n", seq_num);
    } else {
        received_data[seq_num] = strdup(line);
        printf("Пакет с номером %d сохранён: %s\n", seq_num, line);
    }
}

void receive_packets() {
    char buffer[BUFFER_SIZE];
    struct sockaddr_in from_addr;
    socklen_t addr_len = sizeof(from_addr);

    while (1) {
        int len = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&from_addr, &addr_len);
        if (len < 0) {
            perror("Ошибка при получении пакета");
            continue;
        }

        buffer[len] = '\0';

        if (strcmp(buffer, "END") == 0) {
            printf("Получен сигнал завершения передачи. Завершаем приём...\n");
            break;
        }

        int seq_num;
        char line[BUFFER_SIZE];
        sscanf(buffer, "%d %[^\n]", &seq_num, line);

        store_packet(seq_num, line);

        // Отправка ACK
        char ack_message[20];
        snprintf(ack_message, sizeof(ack_message), "ACK %d", seq_num);
        sendto(sockfd, ack_message, strlen(ack_message), 0, (struct sockaddr*)&from_addr, addr_len);
        printf("Отправлен ACK для пакета %d\n", seq_num);
    }
}

void write_to_file(const char* filename) {
    FILE* file = fopen(filename, "w");
    if (file == NULL) {
        perror("Ошибка при открытии файла для записи");
        return;
    }

    printf("Начинаем запись строк в файл...\n");
    for (int i = 0; i < MAX_PACKETS; i++) {
        if (received_data[i] != NULL) {
            fprintf(file, "%s\n", received_data[i]);
            free(received_data[i]);
        }
    }

    fclose(file);
    printf("Файл %s успешно записан.\n", filename);
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <output_file> <ip> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char* output_file = argv[1];
    setlocale(LC_ALL, "");
    init_socket();

    connect_phase();
    receive_packets();
    write_to_file(output_file);
    disconnect_phase();

    close(sockfd);
    return 0;
}