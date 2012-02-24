#include <cstring>
#include <cstdlib>
#include <iostream>

#include "ElevatorCommon.hpp"
#include "ElevatorController.hpp"
#include "UDPView.hpp"

int main(int argc, char* argv[]) {
	if (argc != 5) {
		std::cerr << "USAGE: main <gd_ip> <gd_port> <gui_ip> <gui_port>" << std::endl;
		exit(1);
	}

	ElevatorController* ec = new ElevatorController();
	UDPView* uv = new UDPView(argv[3], argv[4]);

	ec->connectToGD(argv[1], atoi(argv[2]));
	ec->addView(uv);

	uv->receiveMessage(24);
	
	delete ec;
	
	std::cout << std::endl;

	exit(0);
}