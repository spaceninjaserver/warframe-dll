<?php
require "tools/u64_dyn.php";

function joaat(string $str): int
{
	return hexdec(hash("joaat", $str));
}

function get_bootstrapper_title(): string
{
	foreach (explode("\n", file_get_contents("main.hpp")) as $line)
	{
		if (str_starts_with($line, "#define BOOTSTRAPPER_TITLE"))
		{
			return substr($line, 28, -2);
		}
	}
}

$target_version = get_bootstrapper_title();
$code_version = substr($target_version, strlen("OpenWF Bootstrapper v"));
$all_tags = explode("\n", shell_exec("git tag --list"));
$base_tag = in_array($code_version, $all_tags) ? $code_version : "";

chdir("tools");
passthru("pluto archive.pluto $base_tag");
chdir("..");

function wrap_archive($uncompressed)
{
	$bin_str = pack_u64_dyn_bp(time());
	$bin_str .= pack_u64_dyn_bp(strlen($uncompressed));
	$bin_str .= gzdeflate($uncompressed, 9);
	$bin_str .= pack("V", 0);
	return $bin_str;
}

$bin_str = wrap_archive(file_get_contents("archive_all.tmp"));
unlink("archive_all.tmp");

file_put_contents("owf_archive_data.inc", "static const char compressed_archive_data[] = { '\\x".join("', '\\x", array_map("dechex", array_map("ord", str_split($bin_str))))."' };");
touch("owf_archive_data.cpp");

if ($base_tag)
{
	echo ">>> Hotfix.owf only contains files changed since tag ".$code_version."\n";
	$bin_str = wrap_archive(file_get_contents("archive_changed.tmp"));
	unlink("archive_changed.tmp");
}

$hotfix = 1;
while (in_array($code_version."-hotfix-".$hotfix, $all_tags))
{
	++$hotfix;
}
echo ">>> Hotfix.owf automatically versioned to $code_version hotfix $hotfix ($code_version-hotfix-$hotfix)\n";
file_put_contents("Hotfix.owf", pack("VC", joaat($target_version), $hotfix).$bin_str);
