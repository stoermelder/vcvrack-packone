#include "../plugin.hpp"

namespace StoermelderPackOne {

static std::string keyName(int key) {
	const char* k = glfwGetKeyName(key, 0);
	if (k) {
		std::string str = k;
		for (auto& c : str) c = std::toupper(c);
		return str;
	}

	switch (key) {
		case GLFW_KEY_SPACE:			return "SPACE";
		case GLFW_KEY_WORLD_1:			return "W1";
		case GLFW_KEY_WORLD_2:			return "W2";
		case GLFW_KEY_ESCAPE:			return "ESC";
		case GLFW_KEY_F1:				return "F1";
		case GLFW_KEY_F2:				return "F2";
		case GLFW_KEY_F3:				return "F3";
		case GLFW_KEY_F4:				return "F4";
		case GLFW_KEY_F5:				return "F5";
		case GLFW_KEY_F6:				return "F6";
		case GLFW_KEY_F7:				return "F7";
		case GLFW_KEY_F8:				return "F8";
		case GLFW_KEY_F9:				return "F9";
		case GLFW_KEY_F10:				return "F10";
		case GLFW_KEY_F11:				return "F11";
		case GLFW_KEY_F12:				return "F12";
		case GLFW_KEY_F13:				return "F13";
		case GLFW_KEY_F14:				return "F14";
		case GLFW_KEY_F15:				return "F15";
		case GLFW_KEY_F16:				return "F16";
		case GLFW_KEY_F17:				return "F17";
		case GLFW_KEY_F18:				return "F18";
		case GLFW_KEY_F19:				return "F19";
		case GLFW_KEY_F20:				return "F20";
		case GLFW_KEY_F21:				return "F21";
		case GLFW_KEY_F22:				return "F22";
		case GLFW_KEY_F23:				return "F23";
		case GLFW_KEY_F24:				return "F24";
		case GLFW_KEY_F25:				return "F25";
		case GLFW_KEY_UP:				return "UP";
		case GLFW_KEY_DOWN:				return "DOWN";
		case GLFW_KEY_LEFT:				return "LEFT";
		case GLFW_KEY_RIGHT:			return "RIGHT";
		case GLFW_KEY_TAB:				return "TAB";
		case GLFW_KEY_ENTER:			return "ENTER";
		case GLFW_KEY_BACKSPACE:		return "BS";
		case GLFW_KEY_INSERT:			return "INS";
		case GLFW_KEY_DELETE:			return "DEL";
		case GLFW_KEY_PAGE_UP:			return "PG-UP";
		case GLFW_KEY_PAGE_DOWN:		return "PG-DW";
		case GLFW_KEY_HOME:				return "HOME";
		case GLFW_KEY_END:				return "END";
		case GLFW_KEY_PRINT_SCREEN:		return "PRINT";
		case GLFW_KEY_PAUSE:			return "PAUSE";
		case GLFW_KEY_KP_DIVIDE:		return "KP /";
		case GLFW_KEY_KP_MULTIPLY:		return "KP *";
		case GLFW_KEY_KP_SUBTRACT:		return "KP -";
		case GLFW_KEY_KP_ADD:			return "KP +";
		case GLFW_KEY_KP_DECIMAL:		return "KP .";
		default:						return "";
	}
}

static int keyFix(int key) {
	switch (key) {
		case GLFW_KEY_KP_0:				return GLFW_KEY_0;
		case GLFW_KEY_KP_1:				return GLFW_KEY_1;
		case GLFW_KEY_KP_2:				return GLFW_KEY_2;
		case GLFW_KEY_KP_3:				return GLFW_KEY_3;
		case GLFW_KEY_KP_4:				return GLFW_KEY_4;
		case GLFW_KEY_KP_5:				return GLFW_KEY_5;
		case GLFW_KEY_KP_6:				return GLFW_KEY_6;
		case GLFW_KEY_KP_7:				return GLFW_KEY_7;
		case GLFW_KEY_KP_8:				return GLFW_KEY_8;
		case GLFW_KEY_KP_9:				return GLFW_KEY_9;
		case GLFW_KEY_KP_EQUAL:			return GLFW_KEY_EQUAL;
		case GLFW_KEY_KP_ENTER:			return GLFW_KEY_ENTER;
		default:						return key;
	}
}

} // namespace StoermelderPackOne