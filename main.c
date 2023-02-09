#include <pthread.h>
#include <stdio.h>

#include <errno.h>
#include <sys/time.h>
#include <unistd.h>

#define _LGPL_SOURCE
#define RCU_MEMBARRIER
#include <urcu.h>
#include <urcu/cds.h>

static pthread_mutex_t m1;

#define REZ1 0xf00f0000
#define REZ2 0x0000f00f

CDS_LIST_HEAD(t_list);

struct t_node
{
    long long value;
    struct cds_list_head node;
    struct rcu_head rcu_head;
};

static void free_node_rcu(struct rcu_head *head)
{
    struct t_node *node = caa_container_of(head, struct t_node, rcu_head);
    node->value = 0;
    free(node);
}

void *test_l_get(void *arg)
{
    long long i = (long long) arg;
    long long count = 0;
    long long count_elm = 0;

    rcu_register_thread();

    printf("Start GET :%lld\n", i);

    while (pthread_mutex_trylock(&m1) == EBUSY) {
        rcu_read_lock();
        struct t_node *node = NULL;
        count_elm = 0;
        cds_list_for_each_entry_rcu(node, &t_list, node)
        {
            if (node->value != REZ1 && node->value != REZ2) {
                printf("Fatal error :%lld :%lld :%lld\n", i, count, count_elm);
                exit(-1);
            }
            count_elm++;
        }
        count++;
        rcu_read_unlock();
    }
    printf("End GET :%lld :%lld :%lld\n", i, count, count_elm);

    pthread_mutex_unlock(&m1);
    rcu_unregister_thread();
    pthread_exit(NULL);
}

void *test_l_add(void *arg)
{
    long long i = (long long) arg;
    long long count = 0;
    long long count1 = 0;

    rcu_register_thread();

    printf("Start ADD :%lld\n", i);

    while (pthread_mutex_trylock(&m1) == EBUSY) {
        count1++;
        if (count1 % 100 == 0) {
            rcu_read_lock();
            struct t_node *node = NULL;
            cds_list_for_each_entry_rcu(node, &t_list, node)
            {
                if (node->value == REZ2) {
                    cds_list_del_rcu(&node->node);
                    call_rcu(&node->rcu_head, free_node_rcu);
                }
            }
            rcu_read_unlock();
        }
        struct t_node *node = malloc(sizeof(*node));
        if (node) {
            node->value = REZ1;
            cds_list_add_tail_rcu(&node->node, &t_list);
            count++;
        }

        struct t_node *node_tail = malloc(sizeof(*node_tail));
        if (node_tail) {
            node_tail->value = REZ2;
            cds_list_add_tail_rcu(&node_tail->node, &t_list);
        }
    }
    printf("End ADD :%lld :%lld\n", i, count);

    pthread_mutex_unlock(&m1);
    rcu_unregister_thread();
    pthread_exit(NULL);
}

int main()
{
    int num_threads = 6;

    long long i;
    struct t_node *node = NULL;
    pthread_t tid[num_threads];

    create_all_cpu_call_rcu_data(0);
    rcu_register_thread();

    pthread_mutex_init(&m1, NULL);
    pthread_mutex_lock(&m1);

    for (i = 0; i < num_threads; i++) {
        if (i < 1)
            pthread_create(&tid[i], NULL, &test_l_add, (void *) i);
        else
            pthread_create(&tid[i], NULL, &test_l_get, (void *) i);
    }

    sleep(5);
    pthread_mutex_unlock(&m1);

    for (i = 0; i < num_threads; i++) {
        pthread_join(tid[i], NULL);
    }

    long long count = 0;
    long long count1 = 0;
    rcu_read_lock();
    cds_list_for_each_entry_rcu(node, &t_list, node)
    {
        if (node->value == REZ1)
            count++;
        else
            count1++;
        cds_list_del_rcu(&node->node);
        call_rcu(&node->rcu_head, free_node_rcu);
    }
    rcu_read_unlock();
    printf("List empty %lld %lld\n", count, count1);

    pthread_mutex_destroy(&m1);
    rcu_unregister_thread();
    free_all_cpu_call_rcu_data();
}
