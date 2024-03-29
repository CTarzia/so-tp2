#include "node.h"
#include "picosha2.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <cstdlib>
#include <queue>
#include <atomic>
#include <mutex>
#include <mpi.h>
#include <map>
#include <unistd.h>

int total_nodes, mpi_rank;
Block *last_block_in_chain;
map<string,Block> node_blocks;

mutex broadcast;


unsigned int global_difficulty = DEFAULT_DIFFICULTY;
int global_validation = VALIDATION_BLOCKS;

void print() {
  Block* it = last_block_in_chain;

  char str [100];

  sprintf(str, "chain%d.txt", mpi_rank);

  FILE* out = fopen(str, "w");

  do {
    fprintf(out, "{index: %d, hash: %s, previous_hash: %s, node_owner: %d, created_at: %d}\n",
     it->index, it->block_hash, it->previous_block_hash, it->node_owner_number, it->created_at);

    it = &node_blocks[it->previous_block_hash];

  }  while (it->index != 0 );

  fclose(out);
}

//Cuando me llega una cadena adelantada, y tengo que pedir los nodos que me faltan
//Si nos separan más de VALIDATION_BLOCKS bloques de distancia entre las cadenas, se descarta por seguridad
bool verificar_y_migrar_cadena(const Block *rBlock, const MPI_Status *status){

  //DONE: Enviar mensaje TAG_CHAIN_HASH
  int source = status->MPI_SOURCE;

  MPI_Send(rBlock->block_hash, HASH_SIZE, MPI_CHAR,  source, TAG_CHAIN_HASH, MPI_COMM_WORLD );
  printf("[%d] Pedi bloques a %d\n", mpi_rank,source);


  Block *blockchain = new Block[global_validation];

  //DONE: Recibir mensaje TAG_CHAIN_RESPONSE
  MPI_Status st;
  int count = -1;


  MPI_Recv(blockchain, global_validation, *MPI_BLOCK, source, TAG_CHAIN_RESPONSE, MPI_COMM_WORLD, &st);

  MPI_Get_count(&st, *MPI_BLOCK, &count);


  //DONE: Verificar que los bloques recibidos
  //sean válidos y se puedan acoplar a la cadena
  bool result = false;


  if (strcmp(blockchain[0].block_hash, rBlock->block_hash) == 0 && blockchain[0].index == rBlock->index) {
    string hash_buf = string(HASH_SIZE, ' ');
    block_to_hash(&blockchain[0], hash_buf);


    if (hash_buf == blockchain[0].block_hash) {
      int i = 0;
      result = true;

      while (
        !node_blocks.count(blockchain[i].block_hash)
        && blockchain[i].index != 1
        && i+1 < count
        && blockchain[i].previous_block_hash == blockchain[i+1].block_hash
        && blockchain[i].index == blockchain[i+1].index + 1
      ) {
        i++;
      }

      if (node_blocks.count(blockchain[i].block_hash) || blockchain[i].index == 1) {
        // copio  blockchain

        *last_block_in_chain = blockchain[0];

        for (int j = 1; j < i; j++) {
          node_blocks[blockchain[j].block_hash] = blockchain[j];
        }

        result = true;
      }
    }
  }

  delete []blockchain;
  return result;
}

//Verifica que el bloque tenga que ser incluido en la cadena, y lo agrega si corresponde
bool validate_block_for_chain(const Block *rBlock, const MPI_Status *status){
  if(valid_new_block(rBlock)){

    //Agrego el bloque al diccionario, aunque no
    //necesariamente eso lo agrega a la cadena
    node_blocks[string(rBlock->block_hash)]=*rBlock;

    //DONE: Si el índice del bloque recibido es 1
    //y mí último bloque actual tiene índice 0,
    //entonces lo agrego como nuevo último.
    if (rBlock->index == 1 && last_block_in_chain->index == 0) {

      *last_block_in_chain = *rBlock;
      printf("[%d] Agregado a la lista bloque con index %d enviado por %d \n", mpi_rank, rBlock->index,status->MPI_SOURCE);
      return true;
    }

    //DONE: Si el índice del bloque recibido es
    //el siguiente a mí último bloque actual,
    //y el bloque anterior apuntado por el recibido es mí último actual,
    //entonces lo agrego como nuevo último.
    if (rBlock->index == last_block_in_chain->index + 1
     && rBlock->previous_block_hash == last_block_in_chain->block_hash) {
      *last_block_in_chain = *rBlock;
      printf("[%d] Agregado a la lista bloque con index %d enviado por %d \n", mpi_rank, rBlock->index,status->MPI_SOURCE);
      return true;
    }

    //DONE: Si el índice del bloque recibido es
    //el siguiente a mí último bloque actual,
    //pero el bloque anterior apuntado por el recibido no es mí último actual,
    //entonces hay una blockchain más larga que la mía.
    if (rBlock->index == last_block_in_chain->index + 1
     && rBlock->previous_block_hash != last_block_in_chain->block_hash) {
      printf("[%d] Perdí la carrera por uno (%d) contra %d \n", mpi_rank, rBlock->index, status->MPI_SOURCE);
      bool res = verificar_y_migrar_cadena(rBlock,status);
      return res;
     }

    //DONE: Si el índice del bloque recibido es igual al índice de mi último bloque actual,
    //entonces hay dos posibles forks de la blockchain pero mantengo la mía
     if (rBlock->index == last_block_in_chain->index) {
      printf("[%d] Conflicto suave: Conflicto de branch (%d) contra %d \n",mpi_rank,rBlock->index,status->MPI_SOURCE);
      return false;
     }

    //DONE: Si el índice del bloque recibido es anterior al índice de mi último bloque actual,
    //entonces lo descarto porque asumo que mi cadena es la que está quedando preservada.
    if (rBlock->index < last_block_in_chain->index) {
      printf("[%d] Conflicto suave: Descarto el bloque (%d vs %d) contra %d \n",mpi_rank,rBlock->index,last_block_in_chain->index, status->MPI_SOURCE);
      return false;
    }

    //DONE: Si el índice del bloque recibido está más de una posición adelantada a mi último bloque actual,
    //entonces me conviene abandonar mi blockchain actual
    if (rBlock->index > last_block_in_chain->index + 1) {
      printf("[%d] Perdí la carrera por varios contra %d \n", mpi_rank, status->MPI_SOURCE);
      bool res = verificar_y_migrar_cadena(rBlock,status);
      return res;
    }

  }

  printf("[%d] Error duro: Descarto el bloque recibido de %d porque no es válido \n",mpi_rank,status->MPI_SOURCE);
  return false;
}


//Envia el bloque minado a todos los nodos
void broadcast_block(const Block *block){
  //No enviar a mí mismo
  //DONE: Completar

  // MPI_Request requests[total_nodes];

  for( int i = 1; i < total_nodes; i++) {
    int target = (mpi_rank + i) % total_nodes;
    MPI_Send(block, 1, *MPI_BLOCK, target , TAG_NEW_BLOCK, MPI_COMM_WORLD);
  }
}

//Proof of work
//DONE: Advertencia: puede tener condiciones de carrera
void* proof_of_work(void *ptr){
    string hash_hex_str;
    Block block;
    unsigned int mined_blocks = 0;
    while(last_block_in_chain->index < MAX_BLOCKS){

      block = *last_block_in_chain;

      //Preparar nuevo bloque
      block.index += 1;
      block.node_owner_number = mpi_rank;
      block.difficulty = global_difficulty;
      block.created_at = static_cast<unsigned long int> (time(NULL));
      memcpy(block.previous_block_hash,block.block_hash,HASH_SIZE);

      //Agregar un nonce al azar al bloque para intentar resolver el problema
      gen_random_nonce(block.nonce);

      //Hashear el contenido (con el nuevo nonce)
      block_to_hash(&block,hash_hex_str);


      //Contar la cantidad de ceros iniciales (con el nuevo nonce)
      if(solves_problem(hash_hex_str)){

          //Verifico que no haya cambiado mientras calculaba
          broadcast.lock();
          if(last_block_in_chain->index < block.index){
            mined_blocks += 1;
            *last_block_in_chain = block;
            strcpy(last_block_in_chain->block_hash, hash_hex_str.c_str());
            node_blocks[hash_hex_str] = *last_block_in_chain;
            printf("[%d] Agregué un producido con index %d \n",mpi_rank,last_block_in_chain->index);

            //DONE: Mientras comunico, no responder mensajes de nuevos nodos
            broadcast_block(last_block_in_chain);
          }
          broadcast.unlock();
      }

    }

  printf("[%d] Termine: mine %d bloques\n", mpi_rank, mined_blocks);
  print();

  return NULL;
}


int node(){


  char * difficulty = getenv("DIFFICULTY");
  if (difficulty) {
      global_difficulty = atoi(difficulty);
  }

  char * validation = getenv("VALIDATION");
  if (validation) {
      global_validation = atoi(validation);
  }


  if (mpi_rank == 0) {
      printf("Difficulty %d\n", global_difficulty);
      printf("Validation Blocks %d\n", global_validation);
  }

  //Tomar valor de mpi_rank y de nodos totales
  MPI_Comm_size(MPI_COMM_WORLD, &total_nodes);
  MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);

  //La semilla de las funciones aleatorias depende del mpi_ranking
  srand(time(NULL) + mpi_rank);
  printf("[MPI] Lanzando proceso %u\n", mpi_rank);

  last_block_in_chain = new Block;

  string hash_hex_str;

  char * initial_nonce = "0123456789";

  //Inicializo el primer bloque
  last_block_in_chain->index = 0;
  last_block_in_chain->node_owner_number = mpi_rank;
  last_block_in_chain->difficulty = global_difficulty;
  last_block_in_chain->created_at = static_cast<unsigned long int> (time(NULL));
  memset(last_block_in_chain->previous_block_hash,0,HASH_SIZE);
  block_to_hash(last_block_in_chain,hash_hex_str);
  strcpy(last_block_in_chain->block_hash, hash_hex_str.c_str());
  strcpy(last_block_in_chain->nonce, initial_nonce);
  // genenerate own hash.

  //DONE: Crear thread para minar
  pthread_t thread;

  pthread_create(&thread, NULL, proof_of_work, NULL);

  Block rBlock;
  char hash_buf[HASH_SIZE];

  Block* blockchain = new Block[global_validation];
  MPI_Status status;
  int flag;

  while(last_block_in_chain->index < MAX_BLOCKS){

      //DONE: Recibir mensajes de otros nodos
      MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,  &status);
      //DONE: Si es un mensaje de nuevo bloque, llamar a la función
      // validate_block_for_chain con el bloque recibido y el estado de MPI

      if ( status.MPI_TAG == TAG_NEW_BLOCK) {
        broadcast.lock();

        MPI_Recv(&rBlock, 1, *MPI_BLOCK, MPI_ANY_SOURCE, TAG_NEW_BLOCK, MPI_COMM_WORLD, &status);

        printf("[%d] Bloque recibido desde %d\n", mpi_rank, status.MPI_SOURCE);
        validate_block_for_chain(&rBlock, &status);

        broadcast.unlock();
      //DONE: Si es un mensaje de pedido de cadena,
      //responderlo enviando los bloques correspondientes
      } else if ( status.MPI_TAG == TAG_CHAIN_HASH) {
        printf("[%d] Recibi un pedido de HASH_CHAIN de %d\n", mpi_rank, status.MPI_SOURCE);
        MPI_Recv(&hash_buf, HASH_SIZE, MPI_CHAR, MPI_ANY_SOURCE, TAG_CHAIN_HASH, MPI_COMM_WORLD, &status);

        blockchain[0] = node_blocks[hash_buf];

        int count = min(global_validation , (int) blockchain[0].index );
        for (int i = 1; i < count; i++)
        {
          blockchain[i] = node_blocks[blockchain[i-1].previous_block_hash];
        }

        MPI_Send(blockchain, count, *MPI_BLOCK, status.MPI_SOURCE, TAG_CHAIN_RESPONSE, MPI_COMM_WORLD );
        printf("[%d] Envie un HASH_RESPONSE a %d\n", mpi_rank, status.MPI_SOURCE);

      }


  }

  printf("[%d] Esperando mensajes...\n", mpi_rank);
  for (int i = 0; i  < EXTRA_TIME; i += QUANTUM){

      MPI_Iprobe(MPI_ANY_SOURCE, TAG_CHAIN_HASH, MPI_COMM_WORLD, &flag,  &status);
      if (flag) {
          printf("[%d] Recibi un pedido de HASH_CHAIN de %d\n", mpi_rank, status.MPI_SOURCE);
          MPI_Recv(&hash_buf, HASH_SIZE, MPI_CHAR, MPI_ANY_SOURCE, TAG_CHAIN_HASH, MPI_COMM_WORLD, &status);

          blockchain[0] = node_blocks[hash_buf];

          int count = min(global_validation , (int) blockchain[0].index );
          for (int i = 1; i < count; i++)
          {
              blockchain[i] = node_blocks[blockchain[i-1].previous_block_hash];
          }

          MPI_Send(blockchain, count, *MPI_BLOCK, status.MPI_SOURCE, TAG_CHAIN_RESPONSE, MPI_COMM_WORLD );
          printf("[%d] Envie un HASH_RESPONSE a %d\n", mpi_rank, status.MPI_SOURCE);

      }

      usleep(QUANTUM);
  }

  pthread_join(thread, NULL);

  printf("[%d] Mori de verdad\n", mpi_rank);

  delete last_block_in_chain;
  return 0;
}
