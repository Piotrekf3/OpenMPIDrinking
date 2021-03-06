#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <assert.h>

//normal consts
#define SEMCOUNT (int)1
#define FALSE (int)0
#define TRUE (int)1
#define ONE_INT (int)1
#define STRUCTURE_SIZE (int)3
#define ARBITER_SIZE (int)1

//tags
#define WANT_TO_DRINK (int)1
#define ANSWER (int)2
#define TRIGGER (int)3
#define START_DRINKING (int)4
#define GATHER_RANKS (int)5
#define ARRAY (int)6
#define ARBITER_REQUEST (int)7
#define ARBITER_ANSWER (int)8

//answers
#define NO (int)0
#define YES (int)1
#define DOES_NOT_MATTER (int)2
#define NO_DRINK (int)3
#define NOT_EQUAL_INDEX (int)4
#define WE_BEGIN_DRINK (int)5
#define I_AM_NOT_IN_GROUP (int)-100

#define max(a, b) \
	({ __typeof__ (a) _a = (a); \
	 __typeof__ (b) _b = (b); \
	 _a > _b ? _a : _b; })

#define min(a, b) \
	({ __typeof__ (a) _a = (a); \
	 __typeof__ (b) _b = (b); \
	 _a < _b ? _a : _b; })

typedef struct ArbiterRequest
{
	int timestamp;
	int rank;
} ArbiterRequest;

int my_group_index_id;
int semaphore_my_group_index_id;

int i_want_to_drink_id;
int semaphore_drink_id;

int am_i_in_group_id;
int semaphore_am_i_in_group_id;

int all_mates_id;
int semaphore_all_mates_id;

int clock_id;
int semaphore_clock_id;

int i_can_decide;
int start_drinking;

int size, rank, range;

void up(int semid)
{
	struct sembuf buf;
	buf.sem_num = 0;
	buf.sem_op = 1;
	buf.sem_flg = 0;
	//	printf("up %d\n",semop(semid, &buf, 1));
	semop(semid, &buf, 1);
}

void down(int semid)
{
	struct sembuf buf;
	buf.sem_num = 0;
	buf.sem_op = -1;
	buf.sem_flg = 0;
	semop(semid, &buf, 1);
	//	printf("down %d\n", semop(semid, &buf, 1));
}

void Append_To_Query(ArbiterRequest request, ArbiterRequest * query, int * queryFirstIndex, int * queryLastIndex)
{
	query[*queryLastIndex] = request;
	(*queryLastIndex)++;
}

ArbiterRequest Pick_From_Query(ArbiterRequest * query, int * queryFirstIndex, int * queryLastIndex)
{
	if(*queryFirstIndex < *queryLastIndex)
	{
		(*queryFirstIndex)++;
		return query[*queryFirstIndex - 1];
	}
	ArbiterRequest dummy;
	dummy.timestamp = 0;
	dummy.rank = -1;
	return dummy;
}

int sendInt(int *data, int size, int destination, int tag)
{
	int *buf = malloc((size + 1) * sizeof(int));
	memcpy(buf, data, size * sizeof(int));
	down(semaphore_clock_id);
	int *clock = (int *)shmat(clock_id, NULL, 0);
	(*clock)++;
	//	printf("clock = %d\n", *clock);
	memcpy(buf + size, clock, sizeof(int));
	shmdt(clock);
	up(semaphore_clock_id);
	return MPI_Send(buf, size + 1, MPI_INT, destination, tag, MPI_COMM_WORLD);
}

int recvInt(int *data, int size, int source, int tag, MPI_Status *status)
{
	int * buf;
	buf = malloc((size + 1) * sizeof(int));
	int ret = MPI_Recv(buf, size + 1, MPI_INT, source, tag, MPI_COMM_WORLD, status);
	down(semaphore_clock_id);
	int *clock = (int *)shmat(clock_id, NULL, 0);
	*clock = max(*clock, buf[size]) + 1;
	printf("clock = %d\n", *clock);
	shmdt(clock);
	up(semaphore_clock_id);
	memcpy(data, buf, size * sizeof(int));
	free(buf);
	return ret;
}

void Send_To_All(int *buf, int size, int my_rank, int tag)
{
	for (int i = 0; i < size; i++)
	{
		if (i != my_rank)
		{
			sendInt(buf, STRUCTURE_SIZE, i, tag);
			// printf("I sent to %d and my rank is %d\n", i, my_rank);
		}
	}
}

void Send_Trigger_To_Myself(int my_rank, int group_index)
{
	int group_index_answer_rank[STRUCTURE_SIZE];
	group_index_answer_rank[0] = group_index;
	group_index_answer_rank[1] = DOES_NOT_MATTER;
	group_index_answer_rank[2] = my_rank;

	sendInt(group_index_answer_rank, STRUCTURE_SIZE, my_rank, TRIGGER);
	// printf("Trigger sent, my rank is %d\n", my_rank);
}

void Send_To_All_Start_Drinking(int *all_mates, int size, int my_rank, int group_index)
{
	int group_index_answer_rank[STRUCTURE_SIZE];
	group_index_answer_rank[0] = group_index;
	group_index_answer_rank[1] = DOES_NOT_MATTER;
	group_index_answer_rank[2] = my_rank;

	for (int i = 0; i < size; i++)
	{
		int other_rank = all_mates[i];
		if (other_rank != -1)
		{
			printf("INDEX %d\n", other_rank);
			// MPI_Send(group_index_answer_rank, STRUCTURE_SIZE, MPI_INT, my_rank, START_DRINKING, MPI_COMM_WORLD);
			sendInt(group_index_answer_rank, STRUCTURE_SIZE, other_rank, START_DRINKING);
		}
	}
}

void Send_To_All_My_Ranks(int *all_mates, int size, int my_rank, int group_index)
{
	int *message = malloc(sizeof(int) * (size + 1));

	for (int i = 0; i < size; i++)
	{
		message[i] = all_mates[i];
	}
	message[size] = group_index;

	for (int i = 0; i < size; i++)
	{
		int other_rank = all_mates[i];
		if (other_rank != -1)
		{
			sendInt(message, size + 1, other_rank, GATHER_RANKS);
		}
		// printf("I sent to %d and my rank is %d\n", i, my_rank);
	}
}

void Send_I_Am_Not_In_Group(int size, int destination, int my_rank, int group_index)
{
	int *message = malloc((size + 1) * sizeof(int));
	memset(message, -1, size * sizeof(int));
	message[size] = I_AM_NOT_IN_GROUP;
	sendInt(message, size + 1, destination, GATHER_RANKS);
}

int* Leave_Only_Common_Ranks(int *original_mates, int *received_mates, int size)
{
	int control = 0;
	int *common_mates;
	common_mates = malloc(size * sizeof(int));
	memset(common_mates, -1, size * sizeof(int));

	for (int i = 0; i < size; i++)
	{
		for(int j = i + 1; j < size; j++)
		{
			if(original_mates[i] == received_mates[j])
			{
				control = 1;
				break;
			}
		}

		if(control == 1)
		{
			common_mates[i] = original_mates[i];
			control = 0;
		}
	}
	return common_mates;
}

void Add_Mate_To_Group(int mate_rank, int *all_mates, int size)
{
	for (int i = 0; i < size; i++)
	{
		if (all_mates[i] == -1)
		{
			all_mates[i] = mate_rank;
			break;
		}
		else if (all_mates[i] == mate_rank)
		{
			break;
		}
	}
}

void Show_Mates(int *all_mates, int size, int my_rank)
{
	for (int i = 0; i < size; i++)
	{
		if (all_mates[i] != -1)
		{
			printf("My rank is %d, i am with mate %d\n", my_rank, all_mates[i]);
		}
	}
}

int Get_Mates_Count(int *all_mates, int size)
{
	int count = 0;
	for (int i = 0; i < size; i++)
	{
		if(all_mates[i] == -1)
		{
			return count;
		}
		count++;
	}
	return count;
}

int Check_If_I_Can_Decide(int *all_mates, int size, int my_rank)
{
	int min = all_mates[0];
	for (int i = 1; i < size; i++)
	{
		if (all_mates[i] == -1)
		{
			break;
		}
		if (all_mates[i] < min)
		{
			min = all_mates[i];
		}
	}

	if (min > my_rank)
	{
		return YES;
	}
	return NO;
}

int Get_My_Group_Index()
{
	int *my_group_index;

	down(semaphore_my_group_index_id);
	my_group_index = (int *)shmat(my_group_index_id, NULL, 0);
	int group_index = *my_group_index;
	shmdt(my_group_index);
	up(semaphore_my_group_index_id);

	return group_index;
}

void Send_Arbiter_Requests()
{
	//agrawala
	int group_index_answer_rank[STRUCTURE_SIZE];
	group_index_answer_rank[0] = 0;
	down(semaphore_clock_id);
	int *clock = (int *)shmat(clock_id, NULL, 0);
	group_index_answer_rank[1] = *clock;
	shmdt(clock);
	up(semaphore_clock_id);
	group_index_answer_rank[2] = rank;
	Send_To_All(group_index_answer_rank, STRUCTURE_SIZE, rank, ARBITER_REQUEST);
}

void *childThread()
{
	int *i_want_to_drink;
	int *am_i_in_group;
	int *all_mates;

	printf("start losu losu\n");
	down(semaphore_drink_id);
	i_want_to_drink = (int *)shmat(i_want_to_drink_id, NULL, 0);
	// perror("i want to drink error");

	printf("%d\n", *i_want_to_drink);

	while (*i_want_to_drink != YES)
	{
		*i_want_to_drink = rand() % range;
		shmdt(i_want_to_drink);
		up(semaphore_drink_id);
		sleep(0.8);
		down(semaphore_drink_id);
		i_want_to_drink = (int *)shmat(i_want_to_drink_id, NULL, 0);
		// printf("%d\n", *i_want_to_drink);
		// perror("i want to drink error2");
	}

	shmdt(i_want_to_drink);
	up(semaphore_drink_id);

	// printf("Chce pic! %d\n", rank);

	// printf("I send to all and wait for all and my rank is %d\n", rank);

	int group_index = Get_My_Group_Index();

	int group_index_answer_rank[STRUCTURE_SIZE];
	group_index_answer_rank[0] = group_index;
	group_index_answer_rank[1] = DOES_NOT_MATTER;
	group_index_answer_rank[2] = rank;
	Send_To_All(group_index_answer_rank, size, rank, WANT_TO_DRINK);

	down(semaphore_am_i_in_group_id);
	am_i_in_group = (int *)shmat(am_i_in_group_id, NULL, 0);

	while (*am_i_in_group != YES)
	{
		shmdt(am_i_in_group);
		up(semaphore_am_i_in_group_id);
		sleep(0.8);
		down(semaphore_am_i_in_group_id);
		am_i_in_group = (int *)shmat(am_i_in_group_id, NULL, 0);
	}

	shmdt(am_i_in_group);
	up(semaphore_am_i_in_group_id);

	// perror("am_i_in_group_error\n");
	down(semaphore_all_mates_id);
	all_mates = (int *)shmat(all_mates_id, NULL, 0);
	i_can_decide = Check_If_I_Can_Decide(all_mates, size, rank);
	printf("can_decide = %d and my rank is %d\n", i_can_decide, rank);

	start_drinking = NO;

	while (i_can_decide == YES && start_drinking != YES)
	{
		//		printf("I am here and my rank is %d\n", rank);
		start_drinking = rand() % 100;
		//		printf("START DRINKING = %d\n", start_drinking);
		if (start_drinking == YES)
		{
			printf("I DECIDED and my rank is %d\n", rank);
			int group_index = Get_My_Group_Index();
			Send_Trigger_To_Myself(rank, group_index);
			break;
		}

		shmdt(all_mates);
		up(semaphore_all_mates_id);
		sleep(0.8);

		down(semaphore_all_mates_id);
		all_mates = (int *)shmat(all_mates_id, NULL, 0);
		i_can_decide = Check_If_I_Can_Decide(all_mates, size, rank);
	}

	shmdt(all_mates);
	up(semaphore_all_mates_id);

	// if (start_drinking == YES)
	// {
	// 	printf("Start drinking\n");
	// }
	// else
	// {
	// 	printf("Waiting for decision and my rank is %d\n", rank);
	// }

	while (1)
		;
	return NULL;
}

int main(int argc, char **argv)
{

	int provided;
	int ret = MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
	assert(ret == 0 && provided == MPI_THREAD_MULTIPLE);

	range = 100;

	semaphore_my_group_index_id = semget(IPC_PRIVATE, SEMCOUNT, 0666 | IPC_CREAT);
	semctl(semaphore_my_group_index_id, 0, SETVAL, (int)1);

	int *my_group_index;
	my_group_index_id = shmget(IPC_PRIVATE, sizeof(int), 0666 | IPC_CREAT);
	my_group_index = (int *)shmat(my_group_index_id, NULL, 0);
	*my_group_index = 1;
	shmdt(my_group_index);

	clock_id = shmget(IPC_PRIVATE, sizeof(int), 0666 | IPC_CREAT);

	semaphore_clock_id = semget(IPC_PRIVATE, SEMCOUNT, 0666 | IPC_CREAT);
	semctl(semaphore_clock_id, 0, SETVAL, (int)1);

	int *clock;

	clock = (int *)shmat(clock_id, NULL, 0);
	*clock = 0;
	shmdt(clock);

	i_want_to_drink_id = shmget(IPC_PRIVATE, sizeof(int), 0777 | IPC_CREAT);
	// perror("i_want_to_drink_id error");
	// printf("%d\n", i_want_to_drink_id);

	semaphore_drink_id = semget(IPC_PRIVATE, SEMCOUNT, 0666 | IPC_CREAT);
	semctl(semaphore_drink_id, 0, SETVAL, (int)1);

	int *i_want_to_drink;
	i_want_to_drink_id = shmget(IPC_PRIVATE, sizeof(int), 0777 | IPC_CREAT);
	i_want_to_drink = (int *)shmat(i_want_to_drink_id, NULL, 0);
	*i_want_to_drink = NO;
	shmdt(i_want_to_drink);

	semaphore_am_i_in_group_id = semget(IPC_PRIVATE, SEMCOUNT, 0666 | IPC_CREAT);
	semctl(semaphore_am_i_in_group_id, 0, SETVAL, (int)1);

	int *am_i_in_group;
	am_i_in_group_id = shmget(IPC_PRIVATE, sizeof(int), 0777 | IPC_CREAT);
	am_i_in_group = (int *)shmat(am_i_in_group_id, NULL, 0);
	*am_i_in_group = NO;
	shmdt(am_i_in_group);

	MPI_Status status;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);

	int queryIndexLast = 0;
	int queryIndexFirst = 0;
	ArbiterRequest *requestsQuery = malloc(sizeof(*requestsQuery) * size);
	
	semaphore_all_mates_id = semget(IPC_PRIVATE, SEMCOUNT, 0666 | IPC_CREAT);
	semctl(semaphore_all_mates_id, 0, SETVAL, (int)1);

	int *all_mates;
	all_mates_id = shmget(IPC_PRIVATE, size * sizeof(int), 0777 | IPC_CREAT);
	all_mates = (int *)shmat(all_mates_id, NULL, 0);
	memset(all_mates, -1, sizeof(int) * size);
	shmdt(all_mates);

	srand(time(0));

	MPI_Barrier(MPI_COMM_WORLD); //pozostalosc po testach, mysle ze mozna to usunac, ale zobaczymy


	pthread_t thread;
	pthread_create(&thread, NULL, childThread, NULL);
	int group_index_answer_rank[STRUCTURE_SIZE], answer_count = 0, answer_count_gather = 0;
	int arbiter_answer_count = 0;
	int invalid = FALSE;
	int start_drinking_local = NO;
	int *all_mates_temp;
	int mates_count = -1;
	int message_size;
	int *message = malloc(sizeof(int) * (size + 2));

	all_mates_temp = malloc((size + 1) * sizeof(int));
	memset(all_mates_temp, -1, sizeof(int) * (size + 1)); // moz niepotrzebne

	while (1)
	{
		MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_INT, &message_size);

		recvInt(message, message_size, MPI_ANY_SOURCE, MPI_ANY_TAG, &status);

		if(status.MPI_TAG != GATHER_RANKS)
		{
			for(int i = 0; i < STRUCTURE_SIZE; i++)
			{
				group_index_answer_rank[i] = message[i];
			}
		}
		else
		{
			for(int i = 0; i < size + 1; i++)
			{
				all_mates_temp[i] = message[i];
			}
			//			printf("all_mates_temp = %d\n", all_mates_temp[size]);
		}

		printf("I received tag %d from %d and my rank is %d\n", status.MPI_TAG, status.MPI_SOURCE, rank);

		if (status.MPI_TAG == WANT_TO_DRINK)
		{
			down(semaphore_drink_id);
			i_want_to_drink = (int *)shmat(i_want_to_drink_id, NULL, 0);

			down(semaphore_my_group_index_id);
			my_group_index = (int *)shmat(my_group_index_id, NULL, 0);

			if (*i_want_to_drink == YES)
			{
				shmdt(i_want_to_drink);
				up(semaphore_drink_id);

				group_index_answer_rank[0] = *my_group_index;
				group_index_answer_rank[2] = rank;

				if (start_drinking_local == NO)
				{
					if (group_index_answer_rank[0] == *my_group_index)
					{
						down(semaphore_am_i_in_group_id);
						am_i_in_group = (int *)shmat(am_i_in_group_id, NULL, 0);

						down(semaphore_all_mates_id);
						all_mates = (int *)shmat(all_mates_id, NULL, 0);

						Add_Mate_To_Group(status.MPI_SOURCE, all_mates, size);
						//	Show_Mates(all_mates, size, rank);

						shmdt(all_mates);
						up(semaphore_all_mates_id);

						shmdt(am_i_in_group);
						up(semaphore_am_i_in_group_id);

						group_index_answer_rank[1] = YES;
						// printf("I answer YES to  %d and my rank is %d\n", status.MPI_SOURCE, rank);
						sendInt(group_index_answer_rank, STRUCTURE_SIZE, status.MPI_SOURCE, ANSWER);
					}
					else
					{
						group_index_answer_rank[1] = NOT_EQUAL_INDEX;
						// printf("I answer NO2 to  %d and my rank is %d\n", status.MPI_SOURCE, rank);
						sendInt(group_index_answer_rank, STRUCTURE_SIZE, status.MPI_SOURCE, ANSWER);
					}
				}
				else
				{
					group_index_answer_rank[1] = WE_BEGIN_DRINK;
					// printf("I answer NO3 to  %d and my rank is %d\n", status.MPI_SOURCE, rank);
					sendInt(group_index_answer_rank, STRUCTURE_SIZE, status.MPI_SOURCE, ANSWER);
				}
			}
			else
			{
				shmdt(i_want_to_drink);
				up(semaphore_drink_id);

				if (*my_group_index < group_index_answer_rank[0])
				{
					*my_group_index = group_index_answer_rank[0];
				}
				group_index_answer_rank[0] = *my_group_index;
				group_index_answer_rank[1] = NO_DRINK;
				group_index_answer_rank[2] = rank;
				// printf("I answer NO to  %d and my rank is %d\n", status.MPI_SOURCE, rank);
				sendInt(group_index_answer_rank, STRUCTURE_SIZE, status.MPI_SOURCE, ANSWER);
			}
			shmdt(my_group_index);
			up(semaphore_my_group_index_id);
		}
		else if (status.MPI_TAG == ANSWER)
		{
			down(semaphore_my_group_index_id);
			my_group_index = (int *)shmat(my_group_index_id, NULL, 0);

			down(semaphore_all_mates_id);
			all_mates = (int *)shmat(all_mates_id, NULL, 0);
			answer_count++;

			if (group_index_answer_rank[1] == YES && invalid == FALSE)
			{
				Add_Mate_To_Group(status.MPI_SOURCE, all_mates, size);
			}
			else if (group_index_answer_rank[1] != YES && invalid == FALSE)
			{
				if (group_index_answer_rank[1] == NOT_EQUAL_INDEX)
				{
					if (*my_group_index < group_index_answer_rank[0])
						*my_group_index = group_index_answer_rank[0];
					invalid = TRUE;
					memset(all_mates, -1, sizeof(int) * size);
				}
				else if (group_index_answer_rank[1] == WE_BEGIN_DRINK)
				{
					*my_group_index = group_index_answer_rank[0] + 1;
					invalid = TRUE;
					memset(all_mates, -1, sizeof(int) * size);
				}
			}

			if (answer_count == size - 1)
			{
				if (invalid == FALSE)
				{
					printf("All answers came:\n");
					//			Show_Mates(all_mates, size, rank);

					down(semaphore_am_i_in_group_id);
					am_i_in_group = (int *)shmat(am_i_in_group_id, NULL, 0);
					*am_i_in_group = YES;
					shmdt(am_i_in_group);
					up(semaphore_am_i_in_group_id);
				}
				answer_count = 0;
				invalid = FALSE;
			}

			shmdt(my_group_index);
			up(semaphore_my_group_index_id);
			shmdt(all_mates);
			up(semaphore_all_mates_id);
		}
		else if (status.MPI_TAG == TRIGGER)
		{
			start_drinking_local = YES;
			down(semaphore_all_mates_id);
			all_mates = (int *)shmat(all_mates_id, NULL, 0);

			int group_index = Get_My_Group_Index();

			mates_count = Get_Mates_Count(all_mates, size);

			Send_To_All_Start_Drinking(all_mates, size, rank, group_index);
			Send_To_All_My_Ranks(all_mates, size, rank, group_index);

			shmdt(all_mates);
			up(semaphore_all_mates_id);
		}
		else if (status.MPI_TAG == START_DRINKING)
		{
			down(semaphore_am_i_in_group_id);
			am_i_in_group = (int *)shmat(am_i_in_group_id, NULL, 0);
			int group_index = Get_My_Group_Index(); 

			if(*am_i_in_group == YES && group_index_answer_rank[0] == group_index)//zmienic
			{
				start_drinking_local = YES;

				down(semaphore_all_mates_id);
				all_mates = (int *)shmat(all_mates_id, NULL, 0);

				mates_count = Get_Mates_Count(all_mates, size);

				//printf("I send to all my ranks and my rank is %d\n", rank);
				Send_To_All_My_Ranks(all_mates, size, rank, group_index);

				shmdt(all_mates);
				up(semaphore_all_mates_id);
			}
			else
			{
				printf("My group index is %d and my rank is %d\n", group_index_answer_rank[0], rank);
				printf("I_AM_NOT_IN_GROUP and my rank is %d\n", rank);
				Send_I_Am_Not_In_Group(size, status.MPI_SOURCE, rank, group_index);
			}
			/*
			   down(semaphore_all_mates_id);
			   all_mates = (int *)shmat(all_mates_id, NULL, 0);

			   Show_Mates(all_mates, size, rank);

			   shmdt(all_mates);
			   up(semaphore_all_mates_id);
			   */
			shmdt(am_i_in_group);
			up(semaphore_am_i_in_group_id);
		}

		else if (status.MPI_TAG == GATHER_RANKS)
		{
			down(semaphore_am_i_in_group_id);
			am_i_in_group = (int *)shmat(am_i_in_group_id, NULL, 0);
			int group_index = Get_My_Group_Index();

			if(*am_i_in_group == YES && all_mates_temp[size] == group_index)
			{
				down(semaphore_all_mates_id);
				all_mates = (int *)shmat(all_mates_id, NULL, 0);

				if(mates_count == -1)
				{
					mates_count = Get_Mates_Count(all_mates, size);
				}

				start_drinking_local = YES;
				answer_count_gather++;

				printf("Merging my rank is %d\n", rank);

				if(all_mates_temp[size] != I_AM_NOT_IN_GROUP)
				{
					all_mates = Leave_Only_Common_Ranks(all_mates, all_mates_temp, size);
				}

				if(answer_count_gather == mates_count)
				{
					Show_Mates(all_mates, size, rank);

					printf("DRINKING\n");
				}

				shmdt(all_mates);
				up(semaphore_all_mates_id);
			}

			shmdt(am_i_in_group);
			up(semaphore_am_i_in_group_id);
		}
		else if (status.MPI_TAG == ARBITER_REQUEST)
		{
			printf("request\n");
			int localClock;
			down(semaphore_clock_id);
			int *clock = (int *)shmat(clock_id, NULL, 0);
			localClock = *clock;
			shmdt(clock);
			up(semaphore_clock_id);
			if (localClock > group_index_answer_rank[1] || !(i_can_decide && start_drinking))
			{
				sendInt(group_index_answer_rank, STRUCTURE_SIZE, status.MPI_SOURCE, ARBITER_ANSWER);
			}
			else
			{
				ArbiterRequest request;
				request.timestamp = group_index_answer_rank[1];
				request.rank = status.MPI_SOURCE;
				Append_To_Query(request, requestsQuery, &queryIndexFirst, &queryIndexLast);
			}
		}
		else if (status.MPI_TAG == ARBITER_ANSWER)
		{
			arbiter_answer_count++;
			printf("answer\n");
			if(arbiter_answer_count >= size - ARBITER_SIZE)
			{
				printf("Start to drink\n");
				sleep(3);
				arbiter_answer_count = 0;
				start_drinking = NO;
				ArbiterRequest request;
				printf("first = %d, last = %d\n",queryIndexFirst, queryIndexLast);
				for(int i=queryIndexFirst; i<queryIndexLast; i++)
				{
					printf("i=%d\n",i);
					request = Pick_From_Query(requestsQuery, &queryIndexFirst, &queryIndexLast);
					sendInt(group_index_answer_rank, STRUCTURE_SIZE, request.rank, ARBITER_ANSWER);
				}
			}
		}
	}
	printf("I remove shm and semaphores\n");
	pthread_join(thread, NULL);
	semctl(semaphore_drink_id, 0, IPC_RMID);
	shmctl(i_want_to_drink_id, IPC_RMID, NULL);

	semctl(semaphore_am_i_in_group_id, 0, IPC_RMID);
	shmctl(am_i_in_group_id, IPC_RMID, NULL);

	semctl(semaphore_my_group_index_id, 0, IPC_RMID);
	shmctl(my_group_index_id, IPC_RMID, NULL);

	semctl(semaphore_all_mates_id, 0, IPC_RMID);
	shmctl(all_mates_id, IPC_RMID, NULL);

	semctl(semaphore_clock_id, 0, IPC_RMID);
	shmctl(clock_id, IPC_RMID, NULL);

	printf("MPI finallize\n");
	MPI_Finalize();
	return 0;
}
