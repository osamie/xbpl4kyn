#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <cmath>
#include <native/cond.h>
#include <native/mutex.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "ElevatorCommon.hpp"
#include "ElevatorController.hpp"
#include "ElevatorSimulator.hpp"
#include "Exception.hpp"

char ElevatorController::nextID = 1;

ECRTData::ECRTData() {
	mlockall(MCL_CURRENT|MCL_FUTURE);

	rt_mutex_create(&(this->mutex), NULL);
	rt_mutex_create(&(this->mutexBuffer), NULL);
	rt_cond_create(&(this->freeCond), NULL);

	rt_task_create(&(this->ecThread), NULL, 0, 99, T_JOINABLE);
	rt_task_create(&(this->frThread), NULL, 0, 99, T_JOINABLE);
	rt_task_create(&(this->statusThread), NULL, 0, 99, T_JOINABLE);
	rt_task_create(&(this->supervisorThread), NULL, 0, 99, T_JOINABLE);
}

ECRTData::~ECRTData() {
	rt_task_delete(&(this->supervisorThread));
	rt_task_delete(&(this->statusThread));
	rt_task_delete(&(this->frThread));
	rt_task_delete(&(this->ecThread));

	rt_cond_delete(&(this->freeCond));
	rt_mutex_delete(&(this->mutexBuffer));
	rt_mutex_delete(&(this->mutex));
}

ElevatorStatus::ElevatorStatus()
	: currentFloor(0),		direction(DIRECTION_UP),		currentPosition(0),
		currentSpeed(0),		destination(0),							taskActive(false),
		taskAssigned(0),		upDirection(false),					downDirection(false),
		GDFailed(false),		GDFailedEmptyHeap(false),		bufferSelection(0)
{}


ElevatorController::ElevatorController()
	: eStat(), rtData(), downHeap(), upHeap(), missedFloors() {
	this->id = ElevatorController::getNextID();

	if ((this->sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		Die("Failed to create socket");
	}
}

ElevatorController::~ElevatorController() {
	close(this->sock);
}

void ElevatorController::communicate() {
	while (true) {
		try {
			this->waitForGDRequest();
		}
		catch (Exception e) {
			std::cout << e.what() << std::endl;
			exit(1);
		}
	}
}

void ElevatorController::floorRun() {
	unsigned char topItem;
	while(true)
	{
		if(!this->eStat.GDFailedEmptyHeap)
		{
			rt_mutex_acquire(&(this->rtData.mutex), TM_INFINITE);
			rt_cond_wait(&(this->rtData.freeCond), &(this->rtData.mutex), TM_INFINITE);

			int upHeapSize = this->getUpHeap().getSize();
			int downHeapSize = this->getDownHeap().getSize();
			if(upHeapSize==0 && downHeapSize==0){this->eStat.GDFailedEmptyHeap = true;}

			if(this->eStat.downDirection)
			{
				if(downHeapSize > 0)
				{
					topItem = this->getDownHeap().peek();
				}else if(upHeapSize > 0)
				{
					topItem = this->getUpHeap().peek();
				}
			}else
			{
				if(upHeapSize > 0)
				{
					topItem = this->getUpHeap().peek();
				}else if(downHeapSize > 0)
				{
					topItem = this->getDownHeap().peek();
				}
			}

			if(topItem != this->eStat.destination)
			{
				printf("FR%d next Dest is %d\n.", this->getID(), topItem);
				this->getSimulator()->setFinalDestination(topItem);
				this->eStat.destination = topItem;
			}
			rt_mutex_release(&(this->rtData.mutex));
		}
	}
}

void ElevatorController::supervise() {
	while(true)
	{
		rt_mutex_acquire(&(this->rtData.mutex), TM_INFINITE);
		if(this->eStat.GDFailed && this->eStat.GDFailedEmptyHeap)
		{
			if(!this->eStat.taskAssigned)
			{
				if((this->eStat.downDirection && this->eStat.destination!=0) || this->eStat.destination == MAX_FLOORS)
				{
					this->eStat.destination--;
					this->getSimulator()->setFinalDestination(this->eStat.destination);
				}else if(this->eStat.destination == 0 || this->eStat.destination != MAX_FLOORS)
				{
					this->eStat.destination++;
					this->getSimulator()->setFinalDestination(this->eStat.destination);
				}
			}
		}
		rt_mutex_release(&(this->rtData.mutex));
		sleep(20);
	}
}

void ElevatorController::updateStatus() {
	while(true)
	{
		sleep(3);
		rt_mutex_acquire(&(this->rtData.mutex), TM_INFINITE);
		this->eStat.currentFloor = this->getSimulator()->getCurrentFloor();
		this->eStat.taskAssigned = this->getSimulator()->getIsTaskActive();
		if(this->eStat.taskAssigned && (this->getSimulator()->getIsDirectionUp()))
		{
			this->eStat.upDirection = true;
		}
		this->eStat.downDirection = !(this->getSimulator()->getIsDirectionUp());

		if(this->eStat.upDirection){this->eStat.direction = DIRECTION_UP;}
		else{this->eStat.direction = DIRECTION_DOWN;}

		this->eStat.currentPosition = (unsigned char)ceil(this->getSimulator()->geCurrentPosition());
		this->eStat.taskActive = this->eStat.taskAssigned;

		int upHeapSize = this->getUpHeap().getSize();
		int downHeapSize = this->getDownHeap().getSize();
		if(upHeapSize==0 && downHeapSize==0){this->eStat.GDFailedEmptyHeap = true;}

		if(upHeapSize > 0)
		{
			int topItem = (int)(this->getUpHeap().peek());
			if(this->eStat.currentFloor == topItem && !this->eStat.taskAssigned)
			{
				printf("ST%d Task Completed %d\n", this->getID(), this->eStat.destination);
				this->getUpHeap().pop();
			}
		}

		if(downHeapSize > 0)
		{
			int topItem = (int)(this->getDownHeap().peek());
			if(this->eStat.currentFloor == topItem && !this->eStat.taskAssigned)
			{
				printf("ST%d Task Completed %d\n", this->getID(), this->eStat.destination);
				this->getDownHeap().pop();
			}
		}
		this->releaseFreeCond();

		rt_mutex_release(&(this->rtData.mutex));
		this->updateStatusBuffer();
	}
}

bool ElevatorController::releaseFreeCond()
{
	int heapSize = this->getUpHeap().getSize() + this->getDownHeap().getSize();
	if(heapSize > 0)
	{
		this->eStat.GDFailedEmptyHeap = false;
		rt_cond_signal(&(this->rtData.freeCond));
		return true;
	}else
	{
		return false;
	}
}

void ElevatorController::updateStatusBuffer()
{
	//upheap and the downheap (hallcall and floor selections should be included.
	rt_mutex_acquire(&(this->rtData.mutexBuffer), TM_INFINITE);
	bool selectedBuffer = this->eStat.bufferSelection;
	rt_mutex_release(&(this->rtData.mutexBuffer));
	
	rt_mutex_acquire(&(this->rtData.mutex), TM_INFINITE);
	this->eStat.statusBuffer[selectedBuffer][0] = this->eStat.currentFloor;
	this->eStat.statusBuffer[selectedBuffer][1] = this->eStat.direction;
	this->eStat.statusBuffer[selectedBuffer][2] = this->eStat.currentPosition;
	this->eStat.statusBuffer[selectedBuffer][3] = this->eStat.currentSpeed;
	//printf("writting to buffer %d %s\n", selectedBuffer, statusBuffer[selectedBuffer]);
	rt_mutex_release(&(this->rtData.mutex));

	rt_mutex_acquire(&(this->rtData.mutexBuffer), TM_INFINITE);
	this->eStat.bufferSelection = ++(this->eStat.bufferSelection) % 2;
	rt_mutex_release(&(this->rtData.mutexBuffer));
}

void ElevatorController::addSimulator(ElevatorSimulator* es) {
	this->es = es;
}

void ElevatorController::addView(ElevatorControllerView* ecv) {
	this->views.push_back(ecv);
	ecv->setController(this);
}

void ElevatorController::waitForGDRequest() {
	char* request = receiveTCP(MAX_GD_REQUEST_SIZE);
	char requestType = request[0];

	switch (requestType) {
		case STATUS_REQUEST:
			std::cout << "EC" << (unsigned int)this->getID() << ": Status Request" << std::endl;
      this->sendStatus();
			break;
		case HALL_CALL_ASSIGNMENT:
			std::cout << "EC" << (unsigned int)this->getID() << ": Hall Call Assigned: Floor " << (int) request[1] << std::endl;
			this->addHallCall(request[HCA_FLOOR_INDEX], request[HCA_DIRECTION_INDEX]);
			break;
		default:
			std::cout << "EC" << (unsigned int)this->getID() << ": Unknown Message Type" << std::endl;
	}
}

void ElevatorController::sendStatus() {
	// std::vector<char>* hallCalls = new std::vector<char>(); // = FloorRequestHeap.getHallCalls();

	// this->sendMessage(StatusResponseMessage(this->id, 
  //                                         5, 6, 7, hallCalls->size(), 
  //                                         (char*) &hallCalls[0]));
	char len = 1 	/* EC ID */
						+1	/* Message Type */
						+1	/* Position */
						+1	/* Direction */
						+1	/* Is moving? */
						+1	/* num hall calls */
						+0	/* Hall calls */
						+1	/* Num Floor Requests */
						+0	/* Floor requests */
						+1;	/* Terminator */

	char message[len];
	message[0] = STATUS_RESPONSE;
	message[1] = this->id;
	message[2] = 1;
	message[3] = DIRECTION_UP;
	message[4] = 0;
	message[5] = 0;
	message[6] = 0;
	message[7] = MESSAGE_TERMINATOR;

	this->sendMessage(message, len);

	// delete hallCalls;
}

void ElevatorController::connectToGD(char* gdAddress, int port) {
	/* Construct the server sockaddr_in structure */
	memset(&echoserver, 0, sizeof(echoserver));
	echoserver.sin_family = AF_INET;
	echoserver.sin_addr.s_addr = inet_addr(gdAddress);
	echoserver.sin_port = htons(port);

  std::cout << "EC" << (unsigned int)this->getID() << ": Connecting to GroupDispatcher...";
	/* Establish connection */
	if (connect(this->sock,
				(struct sockaddr *) &(echoserver),
			sizeof(echoserver)) < 0) {
		//Die("Failed to connect with server");
	}
  std::cout << "done." << std::endl;
	this->sendRegistration();
}

void ElevatorController::receiveHallCall(HallCallAssignmentMessage& message) {
  std::cout << "EC" << (unsigned int)this->getID() << ": Received hall call for floor " << (int) message.getFloor();
  std::cout << " in " << ((message.getDirection() == DIRECTION_DOWN) ? "downward" : "upward") << " direction" << std::endl;
}

void ElevatorController::sendRegistration() {
  std::cout << "EC" << (unsigned int)this->getID() << ": Sending EC->GD registration...";
	sendMessage(RegisterWithGDMessage(this->getID()));

  std::cout << "done." << std::endl;

	receiveAck();
}

void ElevatorController::receiveAck() {
  std::cout << "EC" << (unsigned int)this->getID() << ": Waitng for EC->GD ack...";
	char* message = this->receiveTCP(2);
  std::cout << "done." << std::endl;
	if (message[0] != REGISTRATION_ACK)
		Die("EC->GD Registration not acknowledged");
}

void ElevatorController::sendMessage(const Message& message) {
	this->sendMessage(message.getBuffer(), message.getLen());
}

void ElevatorController::sendMessage(const char* message, int len) {
	if (len == 0) {
		len = strlen(message);
	}
	/* Send the word to the server */
	if (send(this->sock, message, len, 0) != len) {
		Die("Mismatch in number of sent bytes");
	}
}

char* ElevatorController::receiveTCP(unsigned int length) {
	char* buffer = new char[BUFFSIZE];
	unsigned int received = 0;

	/* Receive the word back from the server */
	int bytes = 0;
	std::cout << "EC" << (unsigned int)this->getID() << ": Waiting for TCP...";
	bytes = recv(this->sock, buffer, BUFFSIZE-1, 0);
	if (bytes <= 0) {
		Die("bytes received error");
	}
	std::cout << "got " << bytes << " bytes" << std::endl;

	std::cout << "EC" << (unsigned int)this->getID() << ": Received: ";
	printBuffer(buffer, bytes);
	std::cout << std::endl;

	return buffer;
}

void ElevatorController::openDoor() {
	std::cout << "EC" << (unsigned int)this->getID() << ": opening door" << std::endl;
}

void ElevatorController::closeDoor() {
	std::cout << "EC" << (unsigned int)this->getID() << ": closing door" << std::endl;
}

void ElevatorController::emergencyStop() {
	std::cout << "EC" << (unsigned int)this->getID() << ": emergency stop" << std::endl;
}

void ElevatorController::addHallCall(unsigned char floor, unsigned char direction) {
	if (direction == DIRECTION_UP) {
		if (es->getCurrentFloor() >= (floor - 1)) { // The elevator cannot stop at the floor
			this->missedFloors.push_back(floor);
		}
		else {
			this->upHeap.pushHallCall(floor);
		}
	}
	else if (direction == DIRECTION_DOWN) {
		if (es->getCurrentFloor() <= (floor + 1)) {
			this->missedFloors.push_back(floor);
		}
		else {
			this->downHeap.pushHallCall(floor);
		}
	}
	else {
		Die("Invalid direction for Hall Call");
	}
}
