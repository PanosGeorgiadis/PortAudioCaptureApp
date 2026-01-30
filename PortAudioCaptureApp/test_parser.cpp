#include "cmd_line_parser.hpp"

using cmd_line_parser::Parser;

auto configure_cmd_line_args(Parser& parser) -> void
{
	parser.add("list devices", "Display list of available audio devices", "-l", false);
}

int main(int argc, char* argv[])
{
	Parser parser(argc, argv);
	configure_cmd_line_args(parser);
	bool parse_success = parser.parse();
	if (!parse_success) {
		return 1;
	}
	parser.get

}