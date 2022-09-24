#include "ros/ros.h"
#include <signal.h>
#include <ros/ros.h>
#include <ros/xmlrpc_manager.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "std_msgs/Bool.h"

// Signal-safe flag for whether shutdown is requested
sig_atomic_t volatile g_request_shutdown = 0;

bool do_blink = false;

// Replacement SIGINT handler
void mySigIntHandler(int sig)
{
	g_request_shutdown = 1;
}

// Replacement "shutdown" XMLRPC callback
void shutdownCallback(XmlRpc::XmlRpcValue& params, XmlRpc::XmlRpcValue& result)
{
	int num_params = 0;
	if (params.getType() == XmlRpc::XmlRpcValue::TypeArray)
		num_params = params.size();
	if (num_params > 1)
	{
		std::string reason = params[1];
		ROS_WARN("Shutdown request received. Reason: [%s]", reason.c_str());
		g_request_shutdown = 1; // Set flag
	}

	result = ros::xmlrpc::responseInt(1, "", 0);
}

void blink_callback(const std_msgs::Bool::ConstPtr& msg)
{
	do_blink = msg->data;
}

int main(int argc, char** argv)
{
	ros::init(argc, argv, "safety_light_node", ros::init_options::NoSigintHandler);
	ros::NodeHandle n(""), np("~");

	int on_microseconds;
	double blink_rate;
	std::string pin;
	std::string gpio_pin;

	np.param("on_microseconds", on_microseconds, 15000);
	np.param("blink_rate", blink_rate, 0.8);
	np.param("pin", pin, std::string("26"));
	
	gpio_pin = "gpio" + pin;

	ROS_INFO("Toggling %s",gpio_pin.c_str());
	ROS_INFO("Rate: %f",blink_rate);
	ROS_INFO("On duration: %f ms",on_microseconds/1000.0);

	ros::Subscriber blink_sub = n.subscribe("safety_light", 1, blink_callback);

	signal(SIGINT, mySigIntHandler);
	ros::XMLRPCManager::instance()->unbind("shutdown");
	ros::XMLRPCManager::instance()->bind("shutdown", shutdownCallback);

	// Export the desired pin by writing to /sys/class/gpio/export
	int fd = open("/sys/class/gpio/export", O_WRONLY);
	if (fd == -1) {
		perror("Unable to open /sys/class/gpio/export");
		exit(1);
	}
	if (write(fd, pin.c_str(), 2) != 2) {
		perror("Error writing to /sys/class/gpio/export");
		exit(1);
	}
	close(fd);

	ros::Duration(0.5).sleep();

	// Set the pin to be an output by writing "out" to /sys/class/gpio/gpio24/direction
	fd = open(("/sys/class/gpio/"+gpio_pin+"/direction").c_str(), O_WRONLY);
	if (fd == -1) {
		perror(("Unable to open /sys/class/gpio/"+gpio_pin+"/direction").c_str());
		exit(1);
	}
	if (write(fd, "out", 3) != 3) {
		perror(("Error writing to /sys/class/gpio/"+gpio_pin+"/direction").c_str());
		exit(1);
	}
	close(fd);

	fd = open(("/sys/class/gpio/"+gpio_pin+"/value").c_str(), O_WRONLY);
	if (fd == -1) {
		perror(("Unable to open /sys/class/gpio/"+gpio_pin+"/value").c_str());
		exit(1);
	}

	write(fd, "1", 1);
	usleep(on_microseconds);
	write(fd, "0", 1);

	// Blink the pin periodically
	ros::Rate loop_rate(blink_rate);
	while (ros::ok() && !g_request_shutdown)
	{
		if(do_blink){
			ROS_INFO("1");

			write(fd, "1", 1);
			usleep(on_microseconds);
			write(fd, "0", 1);

			ROS_INFO("0");	
		}

		ros::spinOnce();
		loop_rate.sleep();
	}

	write(fd, "0", 1);
	close(fd);

	ROS_INFO("shutting down");

	fd = open("/sys/class/gpio/unexport", O_WRONLY);
	if (fd == -1) {
		perror("Unable to open /sys/class/gpio/unexport");
		exit(1);
	}
	if (write(fd, pin.c_str(), 2) != 2) {
		perror("Error writing to /sys/class/gpio/unexport");
		exit(1);
	}
	close(fd);

	ros::shutdown();
}
