#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <sys/resource.h>
#include <assert.h>

#define CITY_A      0b100
#define CITY_B      0b010

struct Car {
    int city;
    int id;
};

struct Element {
    struct Car* car;
    struct Element* next;
};

struct Queue {
    struct Element* head;
    struct Element* tail;
    pthread_mutex_t mutex;
};

int debugMode = 0;
int n = 5;

pthread_mutex_t mutex_bridge;
pthread_cond_t cond_bridge;
struct Car** cars;
struct Car* car_on_bridge = NULL;
struct Queue* queue_cars = NULL;
int* cars_in_queue;

void queue_push (struct Queue* queue, struct Car* car) {
    pthread_mutex_lock(&queue->mutex);
    struct Element* el = malloc(sizeof(struct Element));
    if (el == NULL) {
        perror("malloc:queue_push");
        exit(EXIT_FAILURE);
    }

    el->next = NULL;
    el->car = car;

    cars_in_queue[car->id] = 1;

    if (queue->head == NULL) {
        queue->head = el;
        queue->tail = el;
        pthread_mutex_unlock(&queue->mutex);
        return;
    }

    queue->tail->next = el;
    queue->tail = el;
    pthread_mutex_unlock(&queue->mutex);
}

struct Car* queue_pop (struct Queue* queue) {
    pthread_mutex_lock(&queue->mutex);
    struct Element* head = queue->head;
    struct Car* car = NULL;

    if (head != NULL) {
        queue->head = head->next;
        car = head->car;
        cars_in_queue[car->id] = 0;
        free(head);
    }

    pthread_mutex_unlock(&queue->mutex);
    return car;
}

struct Car* new_car (int id) {
    struct Car* car = malloc(sizeof(struct Car));
    if (car == NULL) {
        perror("malloc:new_car");
        exit(EXIT_FAILURE);
    }

    car->id = id;
    car->city = rand() % 2 ? CITY_A : CITY_B;
    return car;
}

void print () {
    pthread_mutex_lock(&queue_cars->mutex);
    char currentCar[100];
    memset(currentCar, '\0', 100);

    if (car_on_bridge == NULL) {
        currentCar[0] = 'x';
    } else {
        sprintf(currentCar, "%d", car_on_bridge->id);
    }

    int cars_count[] = {
            0, // w b
            0, // w a
            0, // do a
            0, // do b
    };

    for (int i = 0; i < n; ++i) {
        if (cars[i] == NULL) {
            continue;
        }

        int city_index = cars[i]->city >> 2;
        cars_count[city_index] += 1 - cars_in_queue[i];
        cars_count[city_index + 2] += cars_in_queue[i];
    }

    char* direction;
    if (car_on_bridge == NULL) {
        direction = "<>";
    } else if (car_on_bridge->city == CITY_A) {
        direction = ">>";
    } else {
        direction = "<<";
    }

    printf("A-%d %d [%s %s %s] %d %d-B\n", cars_count[1], cars_count[3], direction, currentCar, direction, cars_count[2], cars_count[0]);

    if (debugMode) {
        printf("A do B: ");
        struct Element* curr = queue_cars->head;
        while (curr != NULL) {
            if (curr->car->city == CITY_A) {
                printf("%d ", curr->car->id);
            }
            curr = curr->next;
        }

        printf("\n");

        printf("B do A: ");
        curr = queue_cars->head;
        while (curr != NULL) {
            if (curr->car->city == CITY_B) {
                printf("%d ", curr->car->id);
            }
            curr = curr->next;
        }

        printf("\n");

//        printf("pelna kolejka: ");
//        curr = queue_cars->head;
//        while (curr != NULL) {
//            printf("%d ", curr->car->id);
//            curr = curr->next;
//        }
//
//        printf("\n");
    }

    fflush(stdout);
    pthread_mutex_unlock(&queue_cars->mutex);
}

void city (struct Car* car) {
    // Jezdzimy sobie po miescie
    sleep(1 + rand() % 7);

    // Ustawiamy sie w kolejce
    queue_push(queue_cars, car);
    print();
}

void* car (void* arg) {
    int i = (int) (long) arg;

    while (1) {
        // Wjezdzamy do miasta
        city(cars[i]);

        pthread_mutex_lock(&mutex_bridge);

        // Czekamy na nasza kolej
        while (car_on_bridge != NULL) {
            pthread_cond_wait(&cond_bridge, &mutex_bridge);
        }

        // Scheduler ustawiony na FIFO bedzie dzialal identycznie jak nasza kolejka pod warunkiem
        // ze nie bedziemy lockowac mutexa mostu nigdzie indziej
        car_on_bridge = queue_pop(queue_cars);
        print();

        // Przejezdzamy sobie przez most
        sleep(1);

        // Zmieniamy miasto, CITY_A to 0b100
        // natomiast         CITY_B to 0b010
        // wiec mozemy ladnie graficznie zmienic miasto poprzez przesuniecie bitowe
        if (car_on_bridge->city == CITY_A) {
            car_on_bridge->city >>= 1;
        } else {
            car_on_bridge->city <<= 1;
        }

        // Zjezdzamy z mostu
        car_on_bridge = NULL;
        pthread_mutex_unlock(&mutex_bridge);

        // Informujemy o zjechaniu
        pthread_cond_broadcast(&cond_bridge);
    }
}

int main (int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (!strcmp("-debug", argv[i])) {
            debugMode = 1;
        } else {
            n = strtoumax(argv[optind++], NULL, 10);
            if (n == UINTMAX_MAX && errno == ERANGE || n < 1) {
                fprintf(stderr, "Powinno byc wiecej niz 0 samochodow\n");
                exit(EXIT_FAILURE);
            }
        }
    }

    // Tworzymy kolejke
    queue_cars = malloc(sizeof(struct Queue));
    if (queue_cars == NULL) {
        perror("malloc:queue_cars");
        exit(EXIT_FAILURE);
    }

    queue_cars->head = NULL;
    queue_cars->tail = NULL;
    pthread_mutex_init(&queue_cars->mutex, NULL);
    cars_in_queue = malloc(n * sizeof(int));
    if (cars_in_queue == NULL) {
        perror("malloc:cars_in_queue");
        exit(EXIT_FAILURE);
    }

    memset(cars_in_queue, 0, n);

    srand(time(NULL));

    // Fix dla niektorych systemow, gdzie rlimit jest na 0, dziala tylko z sudo
    // struct rlimit lim;
    // setrlimit(RLIMIT_RTPRIO, &lim);

    pthread_mutex_init(&mutex_bridge, NULL);

    pthread_attr_t attr;
    if (pthread_attr_init(&attr)) {
        perror("pthread_attr_init:main");
        exit(EXIT_FAILURE);
    }

    // Nie dziedziczymy po glownym watku
    if (pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED)) {
        perror("setinheritsched:main");
        exit(EXIT_FAILURE);
    }

    // Kolejkowanie
    if (pthread_attr_setschedpolicy(&attr, SCHED_FIFO)) {
        perror("setschedpolicy:main");
        exit(EXIT_FAILURE);
    }

    // To samo priority dla kazdego watku
    struct sched_param schedParam;
    schedParam.sched_priority = 1;
    if (pthread_attr_setschedparam(&attr, &schedParam)) {
        perror("setschedparam:main");
        exit(EXIT_FAILURE);
    }

    cars = malloc(n * sizeof(struct Car));
    if (cars == NULL) {
        perror("malloc:car");
        exit(EXIT_FAILURE);
    }

    // Tworzymy samochody i pokazujemy do jakich miast naleza
    for (int i = 0; i < n; ++i) {
        cars[i] = new_car(i);
        print();
    }

    // Tworzymy watki samochodow
    pthread_t threads[n];
    for (int i = 0; i < n; ++i) {
        if (pthread_create(&threads[i], &attr, &car, (void*) (long) i)) {
            perror("pthread_create:car");
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < n; ++i) {
        if (pthread_join(threads[i], NULL) != 0) {
            perror("pthread_join:main");
            exit(EXIT_FAILURE);
        }
    }
}