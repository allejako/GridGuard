// GridGuard Concurrent Queue Tests
// Tests thread-safe work distribution - critical for GridGuard's multi-process architecture

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <random>

extern "C" {
    #include "sys/Queue.h"
}

class QueueConcurrentTest : public ::testing::Test {
protected:
    Queue queue;

    void SetUp() override {
        Queue_Initiate(&queue);
    }

    void TearDown() override {
        Queue_Shutdown(&queue);
    }
};

// Test basic queue operations
TEST_F(QueueConcurrentTest, BasicPushPop) {
    int data = 42;
    ASSERT_EQ(Queue_Push(&queue, &data, sizeof(data), DATA_TYPE_REQUEST), 0);

    QueueItem item;
    ASSERT_EQ(Queue_Pop(&queue, &item), 0);
    ASSERT_NE(item.data, nullptr);

    int retrieved = *static_cast<int*>(item.data);
    EXPECT_EQ(retrieved, 42);
    EXPECT_EQ(item.type, DATA_TYPE_REQUEST);

    free(item.data);
}

// Test multiple producers, single consumer (fetcher -> parser pattern)
TEST_F(QueueConcurrentTest, MultipleProducersSingleConsumer) {
    const int numProducers = 4;
    const int itemsPerProducer = 100;
    std::atomic<int> itemsConsumed{0};

    // Consumer thread
    std::thread consumer([&]() {
        for (int i = 0; i < numProducers * itemsPerProducer; i++) {
            QueueItem item;
            if (Queue_Pop(&queue, &item) == 0) {
                itemsConsumed++;
                free(item.data);
            }
        }
    });

    // Producer threads
    std::vector<std::thread> producers;
    for (int p = 0; p < numProducers; p++) {
        producers.emplace_back([&, p]() {
            for (int i = 0; i < itemsPerProducer; i++) {
                int data = p * 1000 + i;
                Queue_Push(&queue, &data, sizeof(data), DATA_TYPE_API_RESPONSE);
            }
        });
    }

    // Wait for all producers to finish
    for (auto& producer : producers) {
        producer.join();
    }

    consumer.join();

    EXPECT_EQ(itemsConsumed.load(), numProducers * itemsPerProducer);
}

// Test single producer, multiple consumers (server -> worker threads pattern)
TEST_F(QueueConcurrentTest, SingleProducerMultipleConsumers) {
    const int numConsumers = 8;
    const int totalItems = 1000;
    std::atomic<int> itemsConsumed{0};

    // Consumer threads
    std::vector<std::thread> consumers;
    for (int c = 0; c < numConsumers; c++) {
        consumers.emplace_back([&]() {
            while (true) {
                QueueItem item;
                if (Queue_Pop(&queue, &item) == 0) {
                    itemsConsumed++;
                    free(item.data);
                } else {
                    break; // Queue shutdown
                }
            }
        });
    }

    // Producer thread
    std::thread producer([&]() {
        for (int i = 0; i < totalItems; i++) {
            int data = i;
            Queue_Push(&queue, &data, sizeof(data), DATA_TYPE_PARSED_DATA);
        }
    });

    producer.join();

    // Give consumers time to process
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    Queue_Shutdown(&queue);

    for (auto& consumer : consumers) {
        consumer.join();
    }

    EXPECT_EQ(itemsConsumed.load(), totalItems);
}

// Test queue under heavy concurrent load (stress test)
TEST_F(QueueConcurrentTest, HighConcurrencyStressTest) {
    const int numProducers = 8;
    const int numConsumers = 8;
    const int itemsPerProducer = 50;

    std::atomic<int> itemsProduced{0};
    std::atomic<int> itemsConsumed{0};

    // Consumers
    std::vector<std::thread> consumers;
    for (int c = 0; c < numConsumers; c++) {
        consumers.emplace_back([&]() {
            while (true) {
                QueueItem item;
                if (Queue_Pop(&queue, &item) == 0) {
                    itemsConsumed++;
                    free(item.data);
                } else {
                    break;
                }
            }
        });
    }

    // Producers
    std::vector<std::thread> producers;
    for (int p = 0; p < numProducers; p++) {
        producers.emplace_back([&]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(1, 10);

            for (int i = 0; i < itemsPerProducer; i++) {
                int data = i;
                Queue_Push(&queue, &data, sizeof(data), DATA_TYPE_ENERGY_PLAN);
                itemsProduced++;

                // Random tiny delay to simulate real work
                std::this_thread::sleep_for(std::chrono::microseconds(dis(gen)));
            }
        });
    }

    // Wait for producers
    for (auto& producer : producers) {
        producer.join();
    }

    // Give consumers time to drain queue
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    Queue_Shutdown(&queue);

    for (auto& consumer : consumers) {
        consumer.join();
    }

    EXPECT_EQ(itemsProduced.load(), numProducers * itemsPerProducer);
    EXPECT_EQ(itemsConsumed.load(), itemsProduced.load());
}

// Test queue behavior when full (producer should block)
TEST_F(QueueConcurrentTest, ProducerBlocksWhenQueueFull) {
    // Fill the queue completely
    for (int i = 0; i < QUEUE_SIZE; i++) {
        int data = i;
        ASSERT_EQ(Queue_Push(&queue, &data, sizeof(data), DATA_TYPE_REQUEST), 0);
    }

    std::atomic<bool> producerBlocked{false};
    std::atomic<bool> producerCompleted{false};

    // Try to push one more item (should block)
    std::thread producer([&]() {
        producerBlocked = true;
        int data = 999;
        Queue_Push(&queue, &data, sizeof(data), DATA_TYPE_REQUEST);
        producerCompleted = true;
    });

    // Give producer time to block
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(producerBlocked.load());
    EXPECT_FALSE(producerCompleted.load());

    // Pop one item to unblock producer
    QueueItem item;
    Queue_Pop(&queue, &item);
    free(item.data);

    producer.join();
    EXPECT_TRUE(producerCompleted.load());
}

// Test consumer blocks when queue is empty
TEST_F(QueueConcurrentTest, ConsumerBlocksWhenQueueEmpty) {
    std::atomic<bool> consumerBlocked{false};
    std::atomic<bool> consumerCompleted{false};

    std::thread consumer([&]() {
        consumerBlocked = true;
        QueueItem item;
        if (Queue_Pop(&queue, &item) == 0) {
            consumerCompleted = true;
            free(item.data);
        }
    });

    // Give consumer time to block
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(consumerBlocked.load());
    EXPECT_FALSE(consumerCompleted.load());

    // Push an item to unblock consumer
    int data = 123;
    Queue_Push(&queue, &data, sizeof(data), DATA_TYPE_PARSED_DATA);

    consumer.join();
    EXPECT_TRUE(consumerCompleted.load());
}

// Test graceful shutdown with pending consumers
TEST_F(QueueConcurrentTest, GracefulShutdownWithPendingConsumers) {
    const int numConsumers = 4;
    std::atomic<int> consumersExited{0};

    std::vector<std::thread> consumers;
    for (int i = 0; i < numConsumers; i++) {
        consumers.emplace_back([&]() {
            QueueItem item;
            while (Queue_Pop(&queue, &item) == 0) {
                free(item.data);
            }
            consumersExited++;
        });
    }

    // Give consumers time to block
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Shutdown should unblock all consumers
    Queue_Shutdown(&queue);

    for (auto& consumer : consumers) {
        consumer.join();
    }

    EXPECT_EQ(consumersExited.load(), numConsumers);
}

// Test data integrity: verify all pushed items are popped correctly
TEST_F(QueueConcurrentTest, DataIntegrityCheck) {
    const int numItems = 500;
    std::atomic<bool> integrityCheckPassed{true};

    std::thread producer([&]() {
        for (int i = 0; i < numItems; i++) {
            Queue_Push(&queue, &i, sizeof(i), DATA_TYPE_REQUEST);
        }
    });

    std::thread consumer([&]() {
        int expectedSum = (numItems * (numItems - 1)) / 2;
        int actualSum = 0;

        for (int i = 0; i < numItems; i++) {
            QueueItem item;
            if (Queue_Pop(&queue, &item) == 0) {
                actualSum += *static_cast<int*>(item.data);
                free(item.data);
            }
        }

        if (actualSum != expectedSum) {
            integrityCheckPassed = false;
        }
    });

    producer.join();
    consumer.join();

    EXPECT_TRUE(integrityCheckPassed.load());
}

// Test mixed data types (simulating GridGuard's pipeline)
TEST_F(QueueConcurrentTest, MixedDataTypes) {
    struct TestData {
        int id;
        DataType type;
    };

    std::vector<TestData> testData = {
        {1, DATA_TYPE_REQUEST},
        {2, DATA_TYPE_API_RESPONSE},
        {3, DATA_TYPE_PARSED_DATA},
        {4, DATA_TYPE_ENERGY_PLAN},
    };

    // Push different data types
    for (const auto& data : testData) {
        ASSERT_EQ(Queue_Push(&queue, (void*)&data.id, sizeof(data.id), data.type), 0);
    }

    // Pop and verify types
    for (const auto& expected : testData) {
        QueueItem item;
        ASSERT_EQ(Queue_Pop(&queue, &item), 0);
        EXPECT_EQ(item.type, expected.type);
        free(item.data);
    }
}

// Test performance under realistic GridGuard workload
TEST_F(QueueConcurrentTest, RealisticGridGuardWorkload) {
    // Simulate fetcher -> parser -> compute pipeline
    const int numWorkItems = 200;
    std::atomic<int> fetcherDone{0};
    std::atomic<int> parserDone{0};

    // Fetcher thread (producer)
    std::thread fetcher([&]() {
        for (int i = 0; i < numWorkItems; i++) {
            int data = i;
            Queue_Push(&queue, &data, sizeof(data), DATA_TYPE_API_RESPONSE);
            fetcherDone++;

            // Simulate API fetch delay
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    });

    // Parser threads (consumers)
    std::vector<std::thread> parsers;
    for (int p = 0; p < 3; p++) {
        parsers.emplace_back([&]() {
            while (true) {
                QueueItem item;
                if (Queue_Pop(&queue, &item) == 0) {
                    parserDone++;

                    // Simulate parsing work
                    std::this_thread::sleep_for(std::chrono::microseconds(5));

                    free(item.data);
                } else {
                    break;
                }
            }
        });
    }

    fetcher.join();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    Queue_Shutdown(&queue);

    for (auto& parser : parsers) {
        parser.join();
    }

    EXPECT_EQ(fetcherDone.load(), numWorkItems);
    EXPECT_EQ(parserDone.load(), numWorkItems);
}
