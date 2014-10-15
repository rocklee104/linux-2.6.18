/*
 * lib/parser.c - simple parser for mount, etc. options.
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/parser.h>
#include <linux/slab.h>
#include <linux/string.h>

/**
 * match_one: - Determines if a string matches a simple pattern
 * @s: the string to examine for presense of the pattern
 * @p: the string containing the pattern
 * @args: array of %MAX_OPT_ARGS &substring_t elements. Used to return match
 * locations.
 *
 * Description: Determines if the pattern @p is present in string @s. Can only
 * match extremely simple token=arg style patterns. If the pattern is found,
 * the location(s) of the arguments will be returned in the @args array.
 */
 /*
  *这个函数比较s是否匹配p中的字符串。如果 
  *1.s, p均是字符串，就使用strcmp比较 
  *2.s中含有数字，例如 s = "xxx 5", p = "xxx %d"
  *  这也是可以匹配的。但是%最多有3个 。数字的起始记录在args[]中。
 */
static int match_one(char *s, char *p, substring_t args[])
{
	char *meta;
	int argc = 0;

	if (!p)
		return 1;

	while(1) {
		int len = -1;
		meta = strchr(p, '%');
		if (!meta)
            //普通字符串比较就使用strcmp
			return strcmp(p, s) == 0;
        
        //如果meta-p == 0，strncmp总是返回0 
		if (strncmp(p, s, meta-p))
            //%之前的字符串不相等。
			return 0;

		s += meta - p;
		p = meta + 1;

		if (isdigit(*p))
            //处理例如%5s这种格式
			len = simple_strtoul(p, &p, 10);
		else if (*p == '%') {
            //处理单纯的字符"%"
			if (*s++ != '%')
				return 0;
			p++;
			continue;
		}

		if (argc >= MAX_OPT_ARGS)
			return 0;

		args[argc].from = s;
		switch (*p++) {
		case 's':
			if (strlen(s) == 0)
				return 0;
			else if (len == -1 || len > strlen(s))
				len = strlen(s);
            //处理例如跳过%4s这种格式的前的4个空格
			args[argc].to = s + len;
			break;
		case 'd':
			simple_strtol(s, &args[argc].to, 0);
			goto num;
		case 'u':
			simple_strtoul(s, &args[argc].to, 0);
			goto num;
		case 'o':
			simple_strtoul(s, &args[argc].to, 8);
			goto num;
		case 'x':
			simple_strtoul(s, &args[argc].to, 16);
		num:
			if (args[argc].to == args[argc].from)
				return 0;
			break;
		default:
			return 0;
		}
		s = args[argc].to;
		argc++;
	}
}

/**
 * match_token: - Find a token (and optional args) in a string
 * @s: the string to examine for token/argument pairs
 * @table: match_table_t describing the set of allowed option tokens and the
 * arguments that may be associated with them. Must be terminated with a
 * &struct match_token whose pattern is set to the NULL pointer.
 * @args: array of %MAX_OPT_ARGS &substring_t elements. Used to return match
 * locations.
 *
 * Description: Detects which if any of a set of token strings has been passed
 * to it. Tokens can include up to MAX_OPT_ARGS instances of basic c-style
 * format identifiers which will be taken into account when matching the
 * tokens, and whose locations will be returned in the @args array.
 */
/*
 *这个函数用于在struct match_token中搜索和与s匹配的token(整形)
*/
int match_token(char *s, match_table_t table, substring_t args[])
{
	struct match_token *p;

	for (p = table; !match_one(s, p->pattern, args) ; p++)
		;

	return p->token;
}

/**
 * match_number: scan a number in the given base from a substring_t
 * @s: substring to be scanned
 * @result: resulting integer on success
 * @base: base to use when converting string
 *
 * Description: Given a &substring_t and a base, attempts to parse the substring
 * as a number in that base. On success, sets @result to the integer represented
 * by the string and returns 0. Returns either -ENOMEM or -EINVAL on failure.
 */
//将substring_t中保存的数字字符串，转换成int型
static int match_number(substring_t *s, int *result, int base)
{
	char *endp;
	char *buf;
	int ret;

	buf = kmalloc(s->to - s->from + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	memcpy(buf, s->from, s->to - s->from);
	buf[s->to - s->from] = '\0';
	*result = simple_strtol(buf, &endp, base);
	ret = 0;
	if (endp == buf)
        //substring_t中字符串长度为0
		ret = -EINVAL;
	kfree(buf);
	return ret;
}

/**
 * match_int: - scan a decimal representation of an integer from a substring_t
 * @s: substring_t to be scanned
 * @result: resulting integer on success
 *
 * Description: Attempts to parse the &substring_t @s as a decimal integer. On
 * success, sets @result to the integer represented by the string and returns 0.
 * Returns either -ENOMEM or -EINVAL on failure.
 */
int match_int(substring_t *s, int *result)
{
	return match_number(s, result, 0);
}

/**
 * match_octal: - scan an octal representation of an integer from a substring_t
 * @s: substring_t to be scanned
 * @result: resulting integer on success
 *
 * Description: Attempts to parse the &substring_t @s as an octal integer. On
 * success, sets @result to the integer represented by the string and returns
 * 0. Returns either -ENOMEM or -EINVAL on failure.
 */
int match_octal(substring_t *s, int *result)
{
	return match_number(s, result, 8);
}

/**
 * match_hex: - scan a hex representation of an integer from a substring_t
 * @s: substring_t to be scanned
 * @result: resulting integer on success
 *
 * Description: Attempts to parse the &substring_t @s as a hexadecimal integer.
 * On success, sets @result to the integer represented by the string and
 * returns 0. Returns either -ENOMEM or -EINVAL on failure.
 */
int match_hex(substring_t *s, int *result)
{
	return match_number(s, result, 16);
}

/**
 * match_strcpy: - copies the characters from a substring_t to a string
 * @to: string to copy characters to.
 * @s: &substring_t to copy
 *
 * Description: Copies the set of characters represented by the given
 * &substring_t @s to the c-style string @to. Caller guarantees that @to is
 * large enough to hold the characters of @s.
 */
void match_strcpy(char *to, substring_t *s)
{
	memcpy(to, s->from, s->to - s->from);
	to[s->to - s->from] = '\0';
}

/**
 * match_strdup: - allocate a new string with the contents of a substring_t
 * @s: &substring_t to copy
 *
 * Description: Allocates and returns a string filled with the contents of
 * the &substring_t @s. The caller is responsible for freeing the returned
 * string with kfree().
 */
char *match_strdup(substring_t *s)
{
	char *p = kmalloc(s->to - s->from + 1, GFP_KERNEL);
	if (p)
		match_strcpy(p, s);
	return p;
}

EXPORT_SYMBOL(match_token);
EXPORT_SYMBOL(match_int);
EXPORT_SYMBOL(match_octal);
EXPORT_SYMBOL(match_hex);
EXPORT_SYMBOL(match_strcpy);
EXPORT_SYMBOL(match_strdup);
