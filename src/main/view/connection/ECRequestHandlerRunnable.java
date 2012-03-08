package main.view.connection;


import java.net.DatagramPacket;


import main.view.ViewControl;
import main.view.connection.UDPConnectionManager;
import main.view.connection.message.ViewMessageParser;
import main.view.connection.message.messageIncoming.DirectionChangeMessage;
import main.view.connection.message.messageIncoming.FloorReachedMessage;
import main.view.connection.message.messageIncoming.RegistrationRequestMessage;
import main.view.connection.message.messageIncoming.ViewMessageIncoming;
import main.view.connection.message.messageOutgoing.RegistrationAcknowledgementMessage;


public class ECRequestHandlerRunnable implements Runnable{

	private DatagramPacket inPacket;
	
	public ECRequestHandlerRunnable(DatagramPacket inPacket){
		this.inPacket = inPacket;
	}

	
	public void run() {
		System.out.print("GUI - message received from EC\n");
		
		//parse the message
		ViewMessageIncoming message = ViewMessageParser.getInstance().parseMessage(inPacket.getData());
		
		
		//handle the message
		if( message instanceof RegistrationRequestMessage){
			//add the elevator to the list of elevators
			int elevatorId = ((RegistrationRequestMessage) message).getElevatorControllerId();
			ViewControl.getInstance().onElevatorRegister(elevatorId, inPacket.getAddress().getHostAddress(), inPacket.getPort());
			UDPConnectionManager.getInstance().sendDataToElevator(elevatorId, new RegistrationAcknowledgementMessage().serialize());
		}else if( message instanceof DirectionChangeMessage){
			DirectionChangeMessage message2 = (DirectionChangeMessage) message;
			ViewControl.getInstance().onElevatorDirectionChange( message2.getElevatorControllerId(), message2.getNewDirection() );
		}else if( message instanceof FloorReachedMessage){
			FloorReachedMessage message2 = (FloorReachedMessage) message;
			ViewControl.getInstance().onFloorReached( message2.getElevatorControllerId(), message2.getFloor() );
		}else{
			System.out.println("GDRequestHandlerRunnable - Unexpected message received");
		}
	}
}