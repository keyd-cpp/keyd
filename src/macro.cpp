#include "keyd.h"
#include "macro.h"
#include "config.h"
#include "utils.hpp"
#include <string>

bool macro::equals(const struct config* cfg, const macro& b) const
{
	if (size != b.size)
		return false;
	for (size_t i = 0; i < size; i++) {
		auto& op = (*this)[i];
		if (op.type != b[i].type)
			return false;
		switch (op.type) {
		case MACRO_KEY_SEQ:
		case MACRO_KEY_TAP:
		case MACRO_HOLD:
		case MACRO_RELEASE:
		case MACRO_UNICODE:
		case MACRO_TIMEOUT:
			if (memcmp(&op, &b[i], sizeof(op)) != 0)
				return false;
			continue;
		case MACRO_COMMAND:
			if (cfg->commands[op.code] != cfg->commands[b[i].code])
				return false;
			continue;
		case MACRO_MAX:
			break;
		}

		die("%s: unhandled op", __FUNCTION__);
	}

	return true;
}

/*
 * Parses expressions of the form: C-t type(hello) enter
 * Returns 0 on success.
 */

static std::vector<::ucmd> cmd_buf;

int macro_parse(std::string_view s, macro& macro, struct config* config, const smart_ptr<env_pack>& cmd_env)
{
	cmd_buf.clear();

	auto& commands = config ? config->commands : cmd_buf;

	std::vector<macro_entry> entries;

	auto ADD_ENTRY = [&] (macro_e t, uint16_t d) -> macro_entry& {
		auto& entry = entries.emplace_back();
		entry.type = t;
		entry.id = d;
		entry.code = d;
		return entry;
	};

	std::string buf;
	while (!(s = s.substr(std::min(s.size(), s.find_first_not_of(C_SPACES)))).empty()) {
		std::string_view tok = s.substr(0, s.find_first_of(C_SPACES));
		const bool is_txt = tok.starts_with("type(") || tok.starts_with("text(") || tok.starts_with("txt(") || tok.starts_with("t(");
		const bool is_cmd = tok.starts_with("cmd(") || tok.starts_with("command(");
		if (is_txt || is_cmd) {
			s.remove_prefix(tok.find_first_of('(') + 1);
			for (size_t i = 0; i < s.size(); i++) {
				if (s[i] == '\\')
					i++;
				else if (s[i] == ')') {
					tok = s.substr(0, i);
					break;
				}
			}
			if (tok.size() == s.size()) {
				err("incomplete macro command found");
				return -1;
			}
			if (is_cmd && commands.size() > INT16_MAX) {
				err("max commands exceeded");
				return -1;
			}
			s.remove_prefix(tok.size() + 1);
			if (is_cmd) {
				ADD_ENTRY(MACRO_COMMAND, commands.size());
				auto cmd = config ? (aux_alloc(), make_string(tok)) : make_string(tok);
				cmd.ptr.shrink(str_escape(cmd.data()) + 1);
				commands.emplace_back(::ucmd{
					.cmd = std::move(cmd),
					.env = cmd_env,
				});
			} else {
				uint32_t codepoint;
				while (int chrsz = utf8_read_char(tok, codepoint)) {
					int xcode;

					if (chrsz == 1 && codepoint < 128) {
						size_t i = 0;
						for (i = 1; i < KEYD_ENTRY_COUNT; i++) {
							const auto name = keycode_table[i].name();
							const char* altname = keycode_table[i].alt_name;
							const char *shiftname = keycode_table[i].shifted_name;

							if (name.size() == 1 && name[0] == tok[0]) {
								ADD_ENTRY(MACRO_KEY_TAP, i).mods = {};
								break;
							}

							if (shiftname && shiftname[0] == tok[0] && shiftname[1] == 0) {
								ADD_ENTRY(MACRO_KEY_TAP, i).mods = { .mods = (1 << MOD_SHIFT), .wildc = 0 };
								break;
							}

							if (altname && altname[0] == tok[0] && altname[1] == 0) {
								ADD_ENTRY(MACRO_KEY_TAP, i).mods = {};
								break;
							}
						}
						if (i == KEYD_ENTRY_COUNT) {
							break;
						}
					} else if ((xcode = unicode_lookup_index(codepoint)) > 0) {
						ADD_ENTRY(MACRO_UNICODE, xcode).id = 0;
					}

					tok.remove_prefix(chrsz);
				}
				if (!tok.empty()) {
					err("invalid macro text found: %.*s", (int)tok.size(), tok.data());
					return -1;
				}
			}
			continue;
		}

		s.remove_prefix(tok.size());
		buf = tok;
		buf.resize(str_escape(buf.data()));
		tok = buf;
		uint8_t mods;
		uint8_t wildc;
		uint16_t code;

		if (parse_key_sequence(tok, &code, &mods, &wildc) == 0 && code) {
			if (wildc) {
				// Wildcard is only allowed in single-key macro
				err("%.*s has a wildcard inside a macro", (int)tok.size(), tok.data());
				return -1;
			}
			entries.emplace_back(macro_entry{
				.type = MACRO_KEY_TAP,
				.id = code,
				.mods = { .mods = mods, .wildc = 0 },
			});
			continue;
		} else if (tok.find_first_of('+') + 1) {
			for (auto key : split_char<'+'>(tok)) {
				if (key.ends_with("ms") && key.find_first_not_of("0123456789") == key.size() - 2)
					ADD_ENTRY(MACRO_TIMEOUT, atoi(key.data()));
				else if (parse_key_sequence(key, &code, &mods, &wildc) == 0 && code && !mods && !wildc)
					ADD_ENTRY(MACRO_HOLD, code);
				else {
					err("%.*s is not a valid compound key or timeout", (int)key.size(), key.data());
					return -1;
				}
			}

			ADD_ENTRY(MACRO_RELEASE, 0);
			continue;
		} else if (tok.ends_with("ms") && tok.find_first_not_of("0123456789") == tok.size() - 2) {
			ADD_ENTRY(MACRO_TIMEOUT, atoi(tok.data()));
			continue;
		} else {
			uint32_t codepoint;
			if (int chrsz = utf8_read_char(tok, codepoint); chrsz + 0u == tok.size()) {
				int xcode;

				if (chrsz == 1 && codepoint < 128) {
					size_t i = 0;
					for (i = 1; i < KEYD_ENTRY_COUNT; i++) {
						const auto name = keycode_table[i].name();
						const char *shiftname = keycode_table[i].shifted_name;

						if (name.size() == 1 && name[0] == tok[0]) {
							ADD_ENTRY(MACRO_KEY_TAP, i).mods = {};
							break;
						}

						if (shiftname && shiftname[0] == tok[0] && shiftname[1] == 0) {
							ADD_ENTRY(MACRO_KEY_TAP, i).mods = { .mods = (1 << MOD_SHIFT), .wildc = 0 };
							break;
						}
					}
					if (i < KEYD_ENTRY_COUNT) {
						continue;
					}
				} else if ((xcode = unicode_lookup_index(codepoint)) > 0) {
					ADD_ENTRY(MACRO_UNICODE, xcode).id = 0;
					continue;
				}
			}
		}

		err("%.*s is not a valid key sequence", (int)tok.size(), tok.data());
		return -1;
	}
	if (entries.empty()) {
		err("empty macro");
		return -1;
	}

	if (entries.size() == 1) {
		macro.entry = entries[0];
		macro.size = 1;
	} else {
		macro.size = entries.size();
		macro.entries = config ? (aux_alloc(), make_buf(entries, +0)) : make_buf(entries, +0);
	}

	return 0;
}

uint64_t macro_execute(void (*output)(uint16_t, uint8_t), const macro& macro, uint64_t timeout, struct config* config)
{
	uint64_t t = 0;
	int hold_start = -1;

	for (size_t i = 0; i < macro.size; i++) {
		const macro_entry *ent = &macro[i];

		switch (ent->type) {
			size_t j;
			uint16_t idx;
			uint8_t codes[4];
			uint16_t code;
			uint8_t mods;

		case MACRO_HOLD:
			if (hold_start == -1)
				hold_start = i;

			output(ent->id, 1);

			break;
		case MACRO_RELEASE:
			if (hold_start != -1) {
				size_t j;

				for (j = hold_start; j < i; j++) {
					const struct macro_entry *ent = &macro[j];
					output(ent->id, 0);
				}

				hold_start = -1;
			}
			break;
		case MACRO_UNICODE:
			idx = ent->code | (ent->id << 16);

			unicode_get_sequence(idx, codes);

			for (j = 0; j < 4; j++) {
				output(codes[j], 1);
				output(codes[j], 0);
			}

			break;
		case MACRO_KEY_SEQ:
		case MACRO_KEY_TAP:
			code = ent->id;
			mods = ent->mods.mods;

			static constexpr std::array<uint16_t, MAX_MOD> def_mods{
				KEY_LEFTALT,
				KEY_LEFTMETA,
				KEY_LEFTSHIFT,
				KEY_LEFTCTRL,
				KEY_RIGHTALT,
			};

			for (j = 0; j < MAX_MOD; j++) {
				uint16_t code = (config ? (config->modifiers[j] ? config->modifiers[j][0] : 0) : def_mods[j]);
				uint8_t mask = 1 << j;

				if (mods & mask && code)
					output(code, 1);
			}

			if (mods && timeout)
				t += timeout, usleep(timeout);

			output(code, 1);
			output(code, 0);

			for (j = 0; j < MAX_MOD; j++) {
				uint16_t code = (config ? (config->modifiers[j] ? config->modifiers[j][0] : 0) : def_mods[j]);
				uint8_t mask = 1 << j;

				if (mods & mask && code)
					output(code, 0);
			}


			break;
		case MACRO_TIMEOUT:
			t += ent->code * 1000;
			usleep(ent->code * 1000);
			break;
		case MACRO_COMMAND:
			extern void execute_command(ucmd& cmd);
			execute_command(config ? config->commands.at(ent->code) : cmd_buf.at(ent->code));
			break;
		default:
			continue;
		}

		if (timeout)
			t += timeout, usleep(timeout);
	}

	cmd_buf.clear();
	return t;
}
