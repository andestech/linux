/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Andes Technology Corporation
 */

#include <linux/elf.h>
#include <linux/printk.h>
#include <linux/binfmts.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/ctype.h>

static const char *strcasechr(const char *str, char c)
{
	const char *s = str;
	for (; tolower(*s) != tolower(c); s++)
		if (*s == '\0')
			return NULL;
	return s;
}

char parse_byte(const char** buf, const char* end)
{
	const char ch = **buf;

	if (end - *buf < sizeof(char))
		pr_warn("Truncated ELF attribute section");
	*buf = *buf + sizeof(char);
	return ch;
}

unsigned int parse_uint32le(const char** buf, const char* end)
{
	const unsigned int i = *((const unsigned int*) *buf);

	if (end - *buf < sizeof(unsigned int))
		pr_warn("Truncated ELF attribute section");
	*buf = *buf + sizeof(unsigned int);
	return i;
}

const char* parse_ntbs(const char** buf, const char* end)
{
	const char* s = (const char*) *buf;
	while (*buf < end && **buf != '\0')
		(*buf)++;

	if (*buf == end)
		pr_warn("Truncated ELF attribute section");

	(*buf)++; // Skip over '\0'
	return s;
}

unsigned long parse_uleb128(const char** buf, const char* end)
{
	unsigned long shift = 0;
	unsigned long value = 0;
	while (*buf < end) {
		value |= (**buf & 0x7f) << shift;
		if ((**buf & 0x80) == 0)
			break;
		*buf += 1;
		shift += 7;
	}

	if (*buf == end)
		pr_warn("Truncated ELF attribute section");

	*buf += 1;

	return value;
}

int parse_decimal(const char** s)
{
	int n = 0;

	if (!isdigit(**s))
		pr_warn("Invalid decimal number `%c'", **s);

	while (isdigit(**s)) {
		n = n * 10 + (**s - '0');
		*s += 1;
	}

	return n;
}

struct riscv_version {
	int major;
	int minor;
};

static bool parse_riscv_isa_version(const char** ver)
{
	struct riscv_version v;

	v.major = parse_decimal(ver);

	if (**ver != 'p') {
		pr_warn("Major version number is not followed by `p'");
		return false;
	}
	*ver += 1;

	v.minor = parse_decimal(ver);

	return true;
}


static void skip_underscores(const char **s)
{
	while (**s == '_')
		(*s)++;
}

static bool riscv_isa_version_backward_compatible(struct riscv_version *elf,
	struct riscv_version *host)
{
	if(elf->major > host->major)
		return false;
	if(elf->major == host->major || elf->minor > host->minor)
		return false;
	return true;
}

static bool riscv_isa_version_exact_compatible(struct riscv_version *elf,
	struct riscv_version *host)
{
	if(elf->major != host->major || elf->minor != host->minor)
		return false;
	return true;
}

typedef bool (*compat_func_t)(struct riscv_version *elf,
	struct riscv_version *host);

static bool riscv_isa_version_compatible(const char** elf_ver,
	const char **host_ver, compat_func_t fn)
{
	struct riscv_version elf;
	struct riscv_version host;

	/*
	 * ISA version is in the format of "<major>p<minor>" where major and
	 * minor numbers are decimal. We don't use strtol() here to avoid
	 * accepting whitespaces or plus/minus signs.
	 */
	elf.major = parse_decimal(elf_ver);
	host.major = parse_decimal(host_ver);

	if (**elf_ver != 'p' || **host_ver != 'p') {
		pr_warn("Major version number is not followed by `p'");
		return false;
	}
	*elf_ver += 1;
	*host_ver += 1;

	elf.minor = parse_decimal(elf_ver);
	host.minor = parse_decimal(host_ver);

	return (*fn)(&elf, &host);
}

static bool riscv_non_standard_compatible(const char **elf_nse,
	const char **host_nse)
{
	/* We need more discussion on non-standard extension processing */
	return true;
}

const char* riscv_extensions = "MAFDQLCBJTPVN";

static inline bool extension_later_than(char elf_ex, char host_ex)
{
	const char* elf = strcasechr(riscv_extensions, elf_ex);
	const char* host = strcasechr(riscv_extensions, host_ex);

	if (elf > host)
		return true;
	return false;
}

static inline bool reach_extension_end(char host_ex)
{
	if (host_ex == 'x' || host_ex == 'X' || host_ex == '\0')
		return true;
	return false;
}

static bool riscv_extension_compatible(const char** elf_isa,
	const char **host_isa)
{
	char ch;
	const char *prev_pos = NULL;
	const char *curr_pos = NULL;

	while (**elf_isa != '\0') {
		if (**elf_isa == 'x' || **elf_isa == 'X')
			return true;

		while (extension_later_than(**elf_isa, **host_isa)) {
			(*host_isa)++;
			parse_riscv_isa_version(host_isa);

			if (reach_extension_end(**host_isa))
				return false;
		}

		curr_pos = strcasechr(riscv_extensions, **elf_isa);
		if (prev_pos && prev_pos > curr_pos) {
			pr_warn("Wrong extension order `%c' after `%c'",
			        *curr_pos, *prev_pos);
			return false;
		}
		prev_pos = curr_pos;

		if (tolower(**elf_isa) < tolower(**host_isa)) {
			/*
			 * According to the law of trichonomy, something went
			 * wrong here.  This ELF has an non-supported extension.
			 * Abort the check.
			 */
			pr_warn("Invalid extension `%c'", **elf_isa);
			return false;
		}

		ch = **elf_isa;
		switch (tolower(ch)) {
		/*
		 * This group of extensions has been frozen, so an exact
		 * match is assumed.
		 */
		case 'm':
		case 'a':
		case 'f':
		case 'd':
		case 'q':
		case 'c':
			(*elf_isa)++;
			(*host_isa)++;
			if (!riscv_isa_version_compatible(elf_isa, host_isa,
				&riscv_isa_version_exact_compatible)) {
				pr_warn("Extension `%c' is not compatible", ch);
				return false;
			}
			break;
		/*
		 * Extensions in this group are development in progress.
		 * Assuming backward compatibility here.
		 */
		case 'l':
		case 'b':
		case 'j':
		case 't':
		case 'p':
		case 'v':
		case 'n':
			(*elf_isa)++;
			(*host_isa)++;
			if (!riscv_isa_version_compatible(elf_isa, host_isa,
				&riscv_isa_version_backward_compatible)) {
				pr_warn("Extension `%c' is not compatible", ch);
				return false;
			}
			break;
		default:
			/* should NOT be here */
			pr_warn("Invalid extension `%c'", ch);
			return false;
		}

		skip_underscores(elf_isa);
		skip_underscores(host_isa);
	}
	return true;
}

#define RISCV_NUM_BASE_ISA 4
static bool riscv_base_isa_compatible(const char** elf_isa,
	const char **host_isa)
{
	/* The ISA string must begin with one of these four */
	const char* base_isas[RISCV_NUM_BASE_ISA] = { "RV32I", "RV32E", "RV64I",
						      "RV128I" };
	int elf_base = -1;
	int host_base = -1;
	int i;
	const char* elf_orig = *elf_isa;

	for (i = 0; i < RISCV_NUM_BASE_ISA; i++) {
		size_t len = strlen(base_isas[i]);
		if (elf_base < 0 && !strncasecmp(*elf_isa, base_isas[i], len)) {
			elf_base = i;
			*elf_isa += len;
		}
		if (host_base < 0 && !strncasecmp(*host_isa, base_isas[i], len)) {
			host_base = i;
			*host_isa += len;
		}
	}

	if (elf_base < 0 || host_base < 0)
		goto out_invalid_base;

	if (elf_base != host_base)
		goto out_invalid_base;

	if (!riscv_isa_version_compatible(elf_isa, host_isa,
					  &riscv_isa_version_exact_compatible)) {
		pr_warn("The version of base ISA `%s' is not compatible",
			base_isas[elf_base]);
		return false;
	}

	skip_underscores(elf_isa);
	skip_underscores(host_isa);

	return true;
out_invalid_base:
	pr_warn("Invalid base ISA `%s'", elf_orig);
	return false;
}

static bool riscv_isa_compatible(const char* elf_isa)
{
	const char *host_isa = elf_platform;

	if (!strcmp(elf_isa, host_isa))
		return true;

	/* The ISA string starts with the base ISA */
	if (!riscv_base_isa_compatible(&elf_isa, &host_isa))
		return false;

	/* Followed by multiple extensions */
	if (!riscv_extension_compatible(&elf_isa, &host_isa))
		return false;

	/* Optionally followed by non-standard extensions */
	if (!riscv_non_standard_compatible(&elf_isa, &host_isa))
		return false;

	return true;
}

static bool riscv_priv_spec_compatible(struct riscv_version *priv)
{
	struct riscv_version host;

	host.major = elf_hwcap2 >> 16;
	host.minor = elf_hwcap2 & 0xFFFF;

	if (!riscv_isa_version_exact_compatible(priv, &host))
		return false;
	return true;
}

#define Tag_File                     1
#define Tag_RISCV_stack_align        4
#define Tag_RISCV_arch               5
#define Tag_RISCV_unaligned_access   6
#define Tag_RISCV_priv_spec          8
#define Tag_RISCV_priv_spec_minor    10
#define Tag_RISCV_priv_spec_revision 12
#define Tag_ict_version              0x8000
#define Tag_ict_model                0x8001

#define Tag_arch_legacy               4
#define Tag_priv_spec_legacy          5
#define Tag_priv_spec_minor_legacy    6
#define Tag_priv_spec_revision_legacy 7
#define Tag_strict_align_legacy       8
#define Tag_stack_align_legacy        9
#define TAG_IGNORE_CASE(tag) \
	case tag: \
		continue;

static int parse_riscv_attributes(const char* buf, const char* end)
{
	unsigned long tag;
	const char* isa;

	while (buf < end) {
		/*
		 * Each attribute is a pair of tag and value. The value can be
		 * either a null-terminated byte string or an ULEB128 encoded
		 * integer depending on the tag.
		 */
		tag = parse_uleb128(&buf, end);
		switch (tag) {
			case Tag_RISCV_arch:
				/* For Tag_arch, parse the arch substring. */
				isa = parse_ntbs(&buf, end);
				if (riscv_isa_compatible(isa))
					return 0;
				break;
			case Tag_RISCV_stack_align:
				parse_uleb128(&buf, end);
				break;
			/* Simply ignore other tags */
			TAG_IGNORE_CASE(Tag_RISCV_priv_spec_revision)
			TAG_IGNORE_CASE(Tag_RISCV_priv_spec)
			TAG_IGNORE_CASE(Tag_RISCV_priv_spec_minor)
			TAG_IGNORE_CASE(Tag_RISCV_unaligned_access)
				continue;
			default:
				pr_warn("Unknown RISCV attribute tag %lu", tag);
				continue;
		}
	}
	return -ENOEXEC;
}


static int parse_legacy_riscv_attributes(const char* buf, const char* end)
{
	unsigned long tag;
	const char* isa;
	struct riscv_version priv = {.major = 0, .minor = 0};
	int total_check = 0;

	while (buf < end) {
		/*
		 * Each attribute is a pair of tag and value. The value can be
		 * either a null-terminated byte string or an ULEB128 encoded
		 * integer depending on the tag.
		 */
		tag = parse_uleb128(&buf, end);
		switch (tag) {
			case Tag_arch_legacy:
				/* For Tag_arch, parse the arch substring. */
				isa = parse_ntbs(&buf, end);
				if (!riscv_isa_compatible(isa))
					return -ENOEXEC;
				total_check++;
				break;
			case Tag_priv_spec_legacy:
				priv.major = parse_uleb128(&buf, end);
				total_check++;
				break;
			case Tag_priv_spec_minor_legacy:
				priv.minor = parse_uleb128(&buf, end);
				total_check++;
				break;
			/* Simply ignore other tags */
			TAG_IGNORE_CASE(Tag_priv_spec_revision_legacy)
			TAG_IGNORE_CASE(Tag_strict_align_legacy)
			TAG_IGNORE_CASE(Tag_stack_align_legacy)
				continue;
			default:
				pr_warn("Unknown RISCV attribute tag %lu", tag);
				continue;
		}

		if (total_check == 3) {
			if (riscv_priv_spec_compatible(&priv))
				return 0;
			break;
		}
	}
	return -ENOEXEC;
}

/*
 * Our toolchain previously used incompatible values for tags and there is no
 * good way to disambiguate them as there is no version information for the
 * attribute per se.

 * We "guess" the format by checking if the value for tag 4 is a string
 * that starts with "rv", in this case it must be the old Tag_arch. Otherwise,
 * we treat the whole attribute section as new.
 */
static bool is_legacy_riscv_attributes(const char* buf, const char* end)
{
	unsigned long tag = parse_uleb128(&buf, end);

	if (tag == 4 && end - buf >= 2 && \
	    strncasecmp((const char*) buf, "rv", 2) == 0)
		return true;
	return false;
}

/*
 * We assume that the subsection started with "riscv", the only subsection we
 * support now, has one attribute entry only.  We don't support finer
 * grainularity (attributes for multiple files, sections or symbols), but
 * recongnize the ELF file as a whole.
 */
static int parse_riscv_subsection(const char* buf, const char* end)
{
	const char* sub_begin = buf;
	const char* sub_end;
	unsigned int len;

	/* The "riscv" subsection must begin with a Tag_File */
	if (parse_uleb128(&buf, end) != Tag_File) {
		pr_warn("No Tag_File in \"riscv\" subsection");
		goto out_noexec;
	}

	/* Followed by a length field including the tag byte */
	len = parse_uint32le(&buf, end);
	if (end - sub_begin < len) {
		pr_warn("Truncated \"riscv\" attribute subsection");
		goto out_noexec;
	}
	sub_end = sub_begin + len;

	/* Followed by the actual RISC-V attributes */
	//if (parse_riscv_attributes(buf, sub_end))
	//	goto out_noexec;
	if (is_legacy_riscv_attributes(buf, sub_end)) {
		if(parse_legacy_riscv_attributes(buf, sub_end))
			goto out_noexec;
	} else if(parse_riscv_attributes(buf, sub_end))
		goto out_noexec;

	return 0;
out_noexec:
	return -ENOEXEC;
}

/*
 * We implement this ELF attribute section with reference to Sec 2.2, ARM IHI
 * 0045E specification, but a slightly restricted form.  For detail please
 * check parse_elf_attribute_subsection.
 */
static int parse_elf_attribute_section(const char* buf, const char* end)
{
	const char version = parse_byte(&buf, end);
	char* sub_begin;
	char* sub_end;
	size_t len;

	/* The first byte must be the ASCII character 'A' */
	if (version != 'A') {
		pr_warn("Unsupported ELF attribute version");
		goto out_noexec;
	}

	/* The section is divided into multiple subsections. */
	while (buf < end) {
		/*
		 * Each subsection begins with a 32-bit unsigned integer
		 * indicating its length (including the length field itself).
		 */
		sub_begin = (char *)buf;
		len = parse_uint32le(&buf, end);
		if (end - sub_begin < len) {
			pr_warn("Truncated ELF attribute subsection");
			goto out_noexec;
		}
		sub_end = sub_begin + len;

		/*
		 * It is followed by the a null-terminated name which determines
		 * how to parse the content.  For now we only support one
		 * subsection named "riscv" here.
		 */
		if (strcmp(buf, "riscv")) {
			pr_warn("Ignore not supported subsection %s", buf);
			buf = sub_end;
			continue;
		}

		buf += strlen("riscv") + 1;
		if(parse_riscv_subsection(buf, sub_end))
			goto out_noexec;

		buf = sub_end;
	}

	return 0;
out_noexec:
	return -ENOEXEC;
}

int arch_elf_pt_proc(void *ehdr, struct elf_phdr *phdr, struct file *elf,
	bool is_interp, void *state)
{
	int retval = 0;
	char *rvattr = NULL;
	loff_t pos;

	if (elf_platform == NULL || elf_hwcap == 0)
		return 0;

	rvattr = kmalloc(phdr->p_filesz, GFP_KERNEL);
	if (!rvattr)
		goto out_oom;

	pos = phdr->p_offset;
	retval = kernel_read(elf, rvattr, phdr->p_filesz, &pos);
	if (retval <= 0)
		goto out_free;

	retval = parse_elf_attribute_section(rvattr, rvattr + phdr->p_filesz);

out_free:
	kfree(rvattr);
out_oom:
	return retval;
}
