#include <libuarm.h>
#include <uARMconst.h>
#include <arch.h>
#include "const.h"
#include "mikabooq.h"
#include "scheduler.h"
#include "exceptions.h"
#include "ssi.h"
#include "interrupts.h"
#include "nucleus.h"
//non so perchè debba stare qua
state_t *int_old 	 = (state_t*) INT_OLDAREA;

//calcola la giusta lista di attesa per il dato device, e ne restituisce un puntatore
struct list_head* select_io_queue(unsigned int dev_type, unsigned int dev_numb) {
	return &device_list[(dev_type-DEV_IL_START)*DEV_PER_INT+dev_numb];
}
//gestisco gli interrupt
void int_handler(){

	int_old->pc -= 4;

	if(current_thread != NULL){
		unsigned int elapsed = getTODLO - current_thread->start_t;
        current_thread->exec_t += elapsed - current_thread->total_t;
        current_thread->total_t = elapsed;
		save_state(int_old, &current_thread->t_s);
	}
	//guardo la causa dell' interrupt
	int cause = getCAUSE();

	// Se la causa dell'interrupt è la linea 0 
		if(CAUSE_IP_GET(cause, IL_IPI)){
			line_handler(IL_IPI);
		} else 
	// linea 1 
		if(CAUSE_IP_GET(cause, IL_CPUTIMER)){
			line_handler(IL_CPUTIMER);
		} else
	//  linea 2 timer 
		if (CAUSE_IP_GET(cause, IL_TIMER)){
			//timer_handler();
		} else
    // linea 3 disk 
		if (CAUSE_IP_GET(cause, IL_DISK)){
			device_handler(IL_DISK);
		} else
    // linea 4 tape
		if (CAUSE_IP_GET(cause, IL_TAPE)){
			device_handler(IL_TAPE);
		} else
    // linea 5 network 
		if (CAUSE_IP_GET(cause, IL_ETHERNET)){
			device_handler(IL_ETHERNET);
		} else
    // linea 6 printer
		if (CAUSE_IP_GET(cause, INT_PRINTER)){
			device_handler(IL_PRINTER);
		} else
    // linea 7 terminal 
		if (CAUSE_IP_GET(cause, INT_TERMINAL)){
			terminal_handler();
		}

	scheduler();
}



//restituisce l'indice del device attivo con priorità maggiore
int get_priority_dev(memaddr* line){
	unsigned int activeBit = 1;
	int i;
	/* Usando una maschera (activeBit) ad ogni iterazione isolo i singoli
	bit dei device della linea. Quando ne trovo uno settato ne restituisco
	l'indice */
	for(i = 0; i < 8; i++){
		if(((*line)&activeBit) == activeBit){
			return i;
		}
		activeBit = activeBit << 1;
	}
	return -1;
}


void timer_handler(){
}

//manda un segnale di acknowledge al thread che è in attesa da un device
void ack(int dev_type, int dev_numb, unsigned int status, memaddr *command_reg){
	(*command_reg) = DEV_C_ACK;
	struct list_head* dev=select_io_queue( dev_type,dev_numb);
	//q2=&device_list[(dev_type-DEV_IL_START)*DEV_PER_INT+dev_numb];
	struct tcb_t * thread_dev=thread_dequeue(dev);
	sys_send_msg(SSI,thread_dev,status);
}



void line_handler(int interruptLineNum){

}

//gestisce un device generico (no terminale)
void device_handler(int dev_type){
	// Uso la MACRO per ottenere la linea di interrupt 
	memaddr *interrupt_line = (memaddr*) CDEV_BITMAP_ADDR(dev_type);
	// Ottengo il device a priorità più alta
	int dev_numb = get_priority_dev(interrupt_line);
	// Ottengo il command register del device
	memaddr *command_reg = (memaddr*) (DEV_REG_ADDR(dev_type, dev_numb) + COMMAND_REG_OFFSET);
	// Ottengo lo status register del device 
	memaddr *status_reg 	= (memaddr*) (DEV_REG_ADDR(dev_type, dev_numb));
	//mando un segnale di ack al thread in attesa
	ack(dev_type, dev_numb, (*status_reg), command_reg);
}
//gestisce il terminale
void terminal_handler(){
	// Uso la MACRO per ottenere la linea di interrupt
	memaddr* interrupt_line = (memaddr*) CDEV_BITMAP_ADDR(IL_TERMINAL);
	// Ottengo il device a priorità più alta 
	int device = get_priority_dev(interrupt_line);
	// Controllo il registro di stato del terminale per sapere se è stata effettuata una lettura o una scrittura 
	memaddr term_reg = (memaddr)  (DEV_REG_ADDR(IL_TERMINAL, device));
	memaddr* status_reg_read = (memaddr*) (term_reg + TERM_STATUS_READ);
	memaddr* command_reg_read = (memaddr*) (term_reg + TERM_COMMAND_READ);
	memaddr* status_reg_write = (memaddr*) (term_reg + TERM_STATUS_WRITE);
	memaddr* command_reg_write  = (memaddr*) (term_reg + TERM_COMMAND_WRITE);
	 //è stata fatta una scrittura
	if(((*status_reg_write) & 0x0F) == DEV_TTRS_S_CHARTRSM){
		ack((IL_TERMINAL), device, ((*status_reg_write)), command_reg_write);
	}
	//è stata fatta una lettura
	else if(((*status_reg_read) & 0x0F) == DEV_TRCV_S_CHARRECV){
		ack(IL_TERMINAL, device, ((*status_reg_read)), command_reg_read);
	}
}
