package util;

public class Constants {

	
	public static final int REQUEST_INTERVAL = 500; //in milliseconds
	public static final int ELEVATOR_COUNT = 8;
	public static final int FLOOR_COUNT = 30;
	
	public static final int MAX_MESSAGE_LENGTH = 100; //in bytes
	
	public static final int GD_TCP_PORT = 5000; //GD to elevator controllers
	public static final int GUI_TO_GD_UDP_PORT = 5001; //GUI to GD
	public static final int GD_TO_GUI_UDP_PORT = 5002; //GD to GUI
	public static final int EC_TO_GUI_UDP_PORT = 5003; //EC to GUI
	
	public static final int DIRECTION_UP = 1;
	public static final int DIRECTION_DOWN = 2;
	public static final int DIRECTION_BOTH = 3;
}