/*****************************************************************************
 * mikabooq.c Year 2017 v.0.1 Luglio, 15 2017                              *
 * Copyright 2017 Simone Berni, Marco Negrini, Dorotea Trestini              *
 *                                                                           *
 * This file is part of MiKABoO.                                             *
 *                                                                           *
 * MiKABoO is free software; you can redistribute it and/or modify it under  *
 * the terms of the GNU General Public License as published by the Free      *
 * Software Foundation; either version 2 of the License, or (at your option) *
 * any later version.                                                        *
 * This program is distributed in the hope that it will be useful, but       *
 * WITHOUT ANY WARRANTY; without even the implied warranty of                *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General *
 * Public License for more details.                                          *
 * You should have received a copy of the GNU General Public License along   *
 * with this program; if not, write to the Free Software Foundation, Inc.,   *
 * 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.                  *
 *****************************************************************************/

#include "const.h"
#include "scheduler.h"
#include "exceptions.h"
#include "ssi.h"
//variabili locali
state_t *tlb_old   = (state_t*) TLB_OLDAREA;
state_t *pgmtrap_old = (state_t*) PGMTRAP_OLDAREA;
state_t *sysbp_old   = (state_t*) SYSBK_OLDAREA;

//salvo stato da uno ad un altro
void save_state(state_t *from, state_t *to){
	to->a1                  = from->a1;
	to->a2                  = from->a2;
	to->a3                  = from->a3;
	to->a4                  = from->a4;
	to->v1                  = from->v1;
	to->v2                  = from->v2;
	to->v3                  = from->v3;
	to->v4                  = from->v4;
	to->v5                  = from->v5;
	to->v6                  = from->v6;
	to->sl                  = from->sl;
	to->fp                  = from->fp;
	to->ip                  = from->ip;
	to->sp                  = from->sp;
	to->lr                  = from->lr;
	to->pc                  = from->pc;
	to->cpsr                = from->cpsr;
	to->CP15_Control        = from->CP15_Control;
	to->CP15_EntryHi        = from->CP15_EntryHi;
	to->CP15_Cause          = from->CP15_Cause;
	to->TOD_Hi              = from->TOD_Hi;
	to->TOD_Low             = from->TOD_Low;
}


//mette un thread nella waitqueue
void put_thread_sleep(struct tcb_t* t){
		//tolgo il thread 
		thread_outqueue(t);
		//e lo metto in attesa
		thread_enqueue(t,&wait_queue);
		//aggiorno i tempi
		t->cpu_time+=getTODLO()-process_TOD;
		if(t==current_thread){
			current_thread=NULL;
		}
		t->t_status=T_STATUS_W4MSG;
		soft_block_count++;
}
//sveglio un thread e lo metto nella readyqueue
void wake_me_up(struct tcb_t* sleeper){
	//tolgo il thread dalla coda nella quale era in attesa
	thread_outqueue(sleeper);
	//e lo metto ready
	thread_enqueue(sleeper,&ready_queue);		
	sleeper->t_status=T_STATUS_READY;	
	soft_block_count--;
}
//controlla che il thread al quale si invia/si riceve un messaggio sia vivo
void check_thread_alive(struct tcb_t * t,int cause){
	if(t!=NULL){
		//se il processo è morto
		if(t->t_status == T_STATUS_NONE){
			//controllo il tipo di errore
			switch(cause){
				case SYS_SEND:
					t->err_numb=ERR_SEND_TO_DEAD;
					break;
				case SYS_RECV:
					t->err_numb=ERR_RECV_FROM_DEAD;
					break;
				default:
					break;
			}
			//ho mandato/ricevuto da un morto, non devo continuare con il codice principale
			scheduler();
		}
	}
}
//se viene chiamato il tlb handler
void tlb_handler(){
	if(current_thread!= NULL){
		//se ha un tlb manager
		if(current_thread->t_pcb->tlb_mgr!=NULL){
			//salvo lo stato della chiamata
			save_state(tlb_old, &(current_thread->t_s));
			//mando messaggio al manager che verrà svegliato
			sys_send_msg(current_thread,current_thread->t_pcb->tlb_mgr,(uintptr_t)tlb_old);
			//setto il t_wait4sender in modo che solo lui possa risvegliarlo
			current_thread->t_wait4sender=current_thread->t_pcb->tlb_mgr;
			//metto nella wait queue
			put_thread_sleep(current_thread);	
		}
        else{
        	//se non ha un tlb manager
            ssi_terminate_thread(current_thread);
            current_thread=NULL;
        }
	}
	scheduler();
}

void pgm_handler(){
    if(current_thread != NULL){
        if(current_thread->t_pcb->prg_mgr!=NULL){
        	//salvo lo stato della chiamata
            save_state(pgmtrap_old, &current_thread->t_s);
			//mando messaggio al manager che verrà svegliato
            sys_send_msg(current_thread,current_thread->t_pcb->prg_mgr,(uintptr_t)pgmtrap_old);  
            //setto il t_wait4sender in modo che solo lui possa risvegliarlo
            current_thread->t_wait4sender=current_thread->t_pcb->prg_mgr;
            //metto nella wait queue
            put_thread_sleep(current_thread);

        }
        else{
        	//se non ha il pgm manager
            ssi_terminate_thread(current_thread);
            current_thread =NULL;
        }
    }
    scheduler();
}


//mando un messaggio ad un thread e in caso lo sveglio
void sys_send_msg(struct tcb_t* sender,struct tcb_t* receiver,unsigned int msg){
	//se il destinatario è in attesa proprio di questo messaggio
	if( (receiver->t_status==T_STATUS_W4MSG) && ( (receiver->t_wait4sender==sender) || (receiver->t_wait4sender==NULL) ) ){
		* (unsigned int *)receiver->t_s.a3=msg;
		receiver->t_s.a1=(unsigned int)sender;
		//lo sveglio
		wake_me_up(receiver);
		receiver->t_s.pc += WORD_SIZE;
	}
	//altrimenti lo incodo normalmente
	else{
		int msg_res;
		msg_res=msgq_add(sender,receiver,msg);
		//coda piena, il messaggio non è stato inviato, la msgsend ritornerà -1
		if(msg_res==-1){
			sender->err_numb=ERR_MSQ_FULL;
		}
	}
}




//gestione system calle breaking point
void sys_bp_handler(){
	//salvo lo stato del thread corrente
	save_state(sysbp_old, &current_thread->t_s);
	//mi salvo i vari campi che mi serviranno
	unsigned int cause = CAUSE_EXCCODE_GET(sysbp_old->CP15_Cause);
	unsigned int a0 = sysbp_old->a1;
	struct tcb_t* a1 =(struct tcb_t *) sysbp_old->a2;
	uintptr_t a2 = sysbp_old->a3;
	// Se l'eccezione è di tipo System call 
	if(cause==EXC_SYSCALL){
    	// Se il processo è in kernel mode gestisce adeguatamente 
		if( (current_thread->t_s.cpsr & STATUS_SYS_MODE) == STATUS_SYS_MODE){
			int msg_res;
			switch(a0){
				//errore
				case 0:
					PANIC();
				//devo inviare un messaggio
				case SYS_SEND:
					//controllo se è una send ad un morto
					check_thread_alive(a1,SYS_SEND);
					//a0 contiene la costante 1 (messaggio inviato) a1 contiene l'indirizzo del thread destinatario a2 contiene il puntatore al messaggio
					if(a1->t_pcb->sys_mgr==current_thread ||a1->t_pcb->prg_mgr==current_thread || a1->t_pcb->tlb_mgr==current_thread ){
						//qui è necessaria solo la wake me up perchè essendo il manager a rispondere al thread, il thread non è in receive
						wake_me_up(a1);
					}
					else{
						//faccio la send
						sys_send_msg(current_thread,a1,a2);
					}
					// Evito che rientri nel codice della syscall
					current_thread->t_s.pc += WORD_SIZE;
					//carico lo stato
					LDST(&current_thread->t_s);
				break;
				//devo ricevere un messaggio
				case SYS_RECV:
					//controllo se è una recv da un morto
					check_thread_alive(a1,SYS_RECV);
					//a0 contiene costante 2 a1 contiene l'indirizzo del mittente(null==tutti) a2 contiene puntatore al campo dove regitrare il messaggio(NULL== non registrare)
					msg_res=msgq_get(&a1,current_thread,(uintptr_t *)a2);
					//non c'è ancora il messaggio
					if (msg_res==-1){
						//setto i campi
						current_thread->t_wait4sender=a1;
						current_thread->t_s.a3=a2;
						//metto il thread in attesa
						put_thread_sleep(current_thread);
					}
					//caso corretto
					else{
						//metto in t_s.a3 il puntatore alla struct messaggio
						current_thread->t_s.a3=a2;
						//la msgrecv ritornerà un puntatore al mittente
						current_thread->t_s.a1=(unsigned int)a1;
						// Evito che rientri nel codice della syscall
						current_thread->t_s.pc += WORD_SIZE;
						//carico il thread
						LDST(&current_thread->t_s);
					}
					break;
				default:
				    //se hanno un sysmgr adeguato
				    if(current_thread->t_pcb->sys_mgr != NULL) {
				    	sys_send_msg(current_thread,current_thread->t_pcb->sys_mgr,(uintptr_t)&(current_thread->t_s));
				    	put_thread_sleep(current_thread);
               		  }
               		//lo uccido
				    else {
			            ssi_terminate_thread(current_thread);
			            current_thread=NULL;
					}
				break; 
			}
		}
		// Se invece è in user mode 
		else if((current_thread->t_s.cpsr & STATUS_USER_MODE) == STATUS_USER_MODE){
			//se è una send o una recv
		    if((a0==SYS_RECV)||(a0==SYS_SEND)){
		    	//viene gestito dal program manager se abilitato
	            if(current_thread->t_pcb->prg_mgr != NULL) {
	                save_state(sysbp_old,pgmtrap_old);
	                pgmtrap_old->CP15_Cause = CAUSE_EXCCODE_SET(pgmtrap_old->CP15_Cause, EXC_RESERVEDINSTR);
	                pgm_handler();
	            }
	            else {
	                ssi_terminate_thread(current_thread);
	                current_thread =NULL;
	            }
	        }
	         //se è compresa tra i servizi dichiarati   
	        else if(a0<MAX_REQUEST_VALUE){
	        	//viene gestito dal sys manager se dichiarato
				if(current_thread->t_pcb->sys_mgr != NULL) {
			    	sys_send_msg(current_thread,current_thread->t_pcb->sys_mgr,(uintptr_t)&(current_thread->t_s));
			    	put_thread_sleep(current_thread);
	       		}
	       		//lo uccido
			    else {
		            ssi_terminate_thread(current_thread);
		            current_thread =NULL;
				}
			}
		}
	// Altrimenti se l'eccezione è di tipo BreakPoint 
	} else if(cause == EXC_BREAKPOINT){
		//TODO
		;
	}
	//richiamo lo scheduler
	scheduler();
}