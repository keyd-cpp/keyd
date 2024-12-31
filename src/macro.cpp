#include "keyd.h"
#include "macro.h"
#include "config.h"

/*
 * Parses expressions of the form: C-t type(hello) enter
 * Returns 0 on success.
 */

int macro_parse(std::string_view s, macro& macro, struct config* config)
{
	constexpr std::string_view spaces = " \t\r\n";

	#define ADD_ENTRY(t, d) macro.emplace_back(macro_entry{.type = t, .id = static_cast<uint16_t>(d), .code = static_cast<uint16_t>(d)})

	std::string buf;
	while (!(s = s.substr(std::min(s.size(), s.find_first_not_of(spaces)))).empty()) {
		std::string_view tok = s.substr(0, s.find_first_of(spaces));
		if (tok.starts_with("cmd(") || tok.starts_with("type(") || tok.starts_with("text(") || tok.starts_with("txt(") || tok.starts_with("t(")) {
			const bool is_cmd = tok.starts_with("cmd(");
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
			if (is_cmd && config->commands.size() > INT16_MAX) {
				err("max commands exceeded");
				return -1;
			}
			buf.assign(tok);
			buf.resize(str_escape(buf.data()));
			s.remove_prefix(tok.size() + 1);
			if (is_cmd) {
				ADD_ENTRY(MACRO_COMMAND, config->commands.size());
				config->commands.emplace_back(::ucmd{
					.uid = config->cfg_use_uid,
					.gid = config->cfg_use_gid,
					.cmd = std::move(buf),
					.env = config->env,
				});
			} else {
				uint32_t codepoint;
				while (int chrsz = utf8_read_char(tok, codepoint)) {
					int xcode;

					if (chrsz == 1 && codepoint < 128) {
						size_t i = 0;
						for (i = 0; i < KEYD_KEY_COUNT; i++) {
							const auto name = keycode_table[i].name();
							const char* altname = keycode_table[i].alt_name;
							const char *shiftname = keycode_table[i].shifted_name;

							if (name.size() == 1 && name[0] == tok[0]) {
								ADD_ENTRY(MACRO_KEYSEQUENCE, i).mods = {};
								break;
							}

							if (shiftname && shiftname[0] == tok[0] && shiftname[1] == 0) {
								ADD_ENTRY(MACRO_KEYSEQUENCE, i).mods = { .mods = MOD_SHIFT, .wildc = 0 };
								break;
							}

							if (altname && altname[0] == tok[0] && altname[1] == 0) {
								ADD_ENTRY(MACRO_KEYSEQUENCE, i).mods = {};
								break;
							}
						}
						if (i == KEYD_KEY_COUNT) {
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
			macro.emplace_back(macro_entry{
				.type = MACRO_KEYSEQUENCE,
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
					for (i = 0; i < KEYD_KEY_COUNT; i++) {
						const auto name = keycode_table[i].name();
						const char *shiftname = keycode_table[i].shifted_name;

						if (name.size() == 1 && name[0] == tok[0]) {
							ADD_ENTRY(MACRO_KEYSEQUENCE, i).mods = {};
							break;
						}

						if (shiftname && shiftname[0] == tok[0] && shiftname[1] == 0) {
							ADD_ENTRY(MACRO_KEYSEQUENCE, i).mods = { .mods = MOD_SHIFT, .wildc = 0 };
							break;
						}
					}
					if (i < KEYD_KEY_COUNT) {
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

	return 0;

	#undef ADD_ENTRY
}

void macro_execute(void (*output)(uint16_t, uint8_t),
		   const macro& macro, size_t timeout, struct config* config)
{
	size_t i;
	int hold_start = -1;

	for (i = 0; i < macro.size(); i++) {
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
		case MACRO_KEYSEQUENCE:
			code = ent->id;
			mods = ent->mods.mods;

			for (j = 0; j < config->modifiers.size(); j++) {
				uint16_t code = config->modifiers[j][0];
				uint8_t mask = 1 << j;

				if (mods & mask)
					output(code, 1);
			}

			if (mods && timeout)
				usleep(timeout);

			output(code, 1);
			output(code, 0);

			for (j = 0; j < config->modifiers.size(); j++) {
				uint16_t code = config->modifiers[j][0];
				uint8_t mask = 1 << j;

				if (mods & mask)
					output(code, 0);
			}


			break;
		case MACRO_TIMEOUT:
			usleep(ent->code * 1000);
			break;
		case MACRO_COMMAND:
			extern void execute_command(ucmd& cmd);
			if (config)
				execute_command(config->commands.at(ent->code));
			break;
		default:
			continue;
		}

		if (timeout)
			usleep(timeout);
	}
}
