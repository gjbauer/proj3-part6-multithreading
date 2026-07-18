#!/usr/bin/perl
use 5.16.0;
use warnings FATAL => 'all';

use Test::Simple tests => 55;
use IO::Handle;

ok(!-e "fuse", "no binaries");

system("(make clean 2>&1) > /dev/null");

system("(make fuse format 2>&1) > /dev/null");

system("(mkdir mnt 2>&1) > /dev/null");

sub mount {
    my $log_file = "test.log";
    my $pid = fork();

    if (!defined $pid) {
        die "Fork failed: $!";
    }

    if ($pid == 0) {
        # CHILD PROCESS
        # 1. Open the log file for appending
        open(my $fh, ">>", $log_file) or die "Can't open $log_file: $!";
        
        # 2. Redirect STDOUT and STDERR to the log file
        open(STDOUT, ">&", $fh) or die "Can't dup STDOUT: $!";
        open(STDERR, ">&", $fh) or die "Can't dup STDERR: $!";
        
        # 3. Replace child process with FUSE mount
        exec("./fuse", "-f", "mnt", "my.img") or die "Exec failed: $!";
    }

    # PARENT PROCESS
    sleep 10;
    
    return $pid;
}

sub kill_mount {
    my ($pid) = @_;
    kill 9, $pid;
    waitpid($pid, 0);  # Reap the zombie
}

sub unmount {
    system("(sudo umount -f mnt 2>&1) >> test.log");
    sleep 1;
}

sub write_text {
    my ($name, $data) = @_;
    open my $fh, ">", "mnt/$name" or return;
    $fh->say($data);
    close $fh;
}

sub read_text {
    my ($name) = @_;
    open my $fh, "<", "mnt/$name" or return "";
    local $/ = undef;
    my $data = <$fh> || "";
    close $fh;
    $data =~ s/\s*$//;
    return $data;
}

sub read_text_slice {
    my ($name, $count, $offset) = @_;
    open my $fh, "<", "mnt/$name" or return "";
    my $data;
    seek $fh, $offset, 0;
    read $fh, $data, $count;
    close $fh;
    return $data;
}

system("rm -f test.log");

say "#           == Basic Tests ==";
my $pid = mount();

my $part1 = 0;

sub p1ok {
    my ($cond, $msg) = @_;
    if ($cond) {
        ++$part1;
    }
    else {
        ok(0, $msg);
    }
}

my $msg0 = "hello, one";
write_text("one.txt", $msg0);
ok(-e "mnt/one.txt", "File1 exists.");
p1ok(-f "mnt/one.txt", "File1 is regular file.");
my $msg1 = read_text("one.txt");
say "# '$msg0' eq '$msg1'?";
p1ok($msg0 eq $msg1, "read back data1");

my $msg2 = "hello, two";
write_text("two.txt", $msg2);
p1ok(-e "mnt/two.txt", "File2 exists.");
p1ok(-f "mnt/two.txt", "File2 is regular file.");
my $msg3 = read_text("two.txt");
say "# '$msg0' eq '$msg1'?";
p1ok($msg2 eq $msg3, "Read back data2 correctly.");

my $files = `ls mnt`;
p1ok($files =~ /one\.txt/, "one.txt is in the directory");
p1ok($files =~ /two\.txt/, "two.txt is in the directory");

my $long0 = "=This string is fourty characters long.=" x 50;
write_text("2k.txt", $long0);
my $long1 = read_text("2k.txt");
p1ok($long0 eq $long1, "Read back long correctly.");

my $long2 = read_text_slice("2k.txt", 10, 50);
my $right = "ng is four";
p1ok($long2 eq $right, "Read with offset & length");

say "# part1 = $part1";
ok($part1 == 9, "No regressions on the easy stuff.");

kill_mount($pid);
unmount();

ok(!-e "mnt/one.txt", "one.txt doesn't exist after umount");
$files = `ls mnt`;
ok($files !~ /one\.txt/, "one.txt is not in the directory");
ok($files !~ /two\.txt/, "two.txt is not in the directory");

$pid = mount();

$files = `ls mnt`;
ok($files =~ /one\.txt/, "one.txt is in the directory still");
ok($files =~ /two\.txt/, "two.txt is in the directory still");

my $exit_status = system("rm mnt/one.txt") >> 8;
$files = `ls mnt`;
ok($exit_status eq 0 && $files !~ /one\.txt/, "deleted one.txt");

kill_mount($pid);
unmount();
$pid = mount();

if ($exit_status eq 0) {
    $files = `ls mnt`;
    ok($files !~ /one\.txt/, "one.txt is not present after re-mount");
}


# Journal only covers metadata, so we have to write the data again
write_text("two.txt", $msg2);

$exit_status = system("mv mnt/two.txt mnt/abc.txt") >> 8;
ok($exit_status eq 0, "moved two.txt");
$files = `ls mnt`;
ok($files =~ /abc\.txt/, "have abc.txt");

my $msg4 = read_text("abc.txt");
chomp($msg2);
chomp($msg4);
say "# msg2 length: " . length($msg2) . ", msg4 length: " . length($msg4);
say "# '$msg2' eq '$msg4'?";
ok($msg2 eq $msg4, "Read back data after rename.");

say "#           == Less Basic Tests ==";

system("ln mnt/abc.txt mnt/def.txt");
my $msg5 = read_text("def.txt");
say "# '$msg2' eq '$msg5'?";
ok($msg2 eq $msg5, "Read back data after link.");

system("rm -f mnt/abc.txt");
my $msg6 = read_text("def.txt");
say "# '$msg2' eq '$msg6'?";
ok($msg2 eq $msg6, "Read back data after other link deleted.");

system("mkdir mnt/foo");
ok(-d "mnt/foo", "Made a directory");

kill_mount($pid);
unmount();
$pid = mount();

ok(-d "mnt/foo", "Directory persists after remount");

# Journal only covers metadata, so we have to write the data again
write_text("def.txt", $msg2);

system("cp mnt/def.txt mnt/foo/abc.txt");
my $msg7 = read_text("foo/abc.txt");
say "# msg2 length: " . length($msg2) . ", msg4 length: " . length($msg7);
say "# '$msg2' eq '$msg7'?";
ok($msg2 eq $msg7, "Read back data from copy in subdir.");

my $huge0 = "=This string is fourty characters long.=" x 1000;
write_text("40k.txt", $huge0);

my $huge1 = read_text("40k.txt");
ok($huge0 eq $huge1, "Read back 40k correctly.");

my $huge2 = read_text_slice("40k.txt", 10, 8050);
$right = "ng is four";
ok($huge2 eq $right, "Read with offset & length");

system("mkdir -p mnt/dir1/dir2/dir3/dir4/dir5");
my $hi0 = "hello there";
write_text("dir1/dir2/dir3/dir4/dir5/hello.txt", $hi0);
my $hi1 = read_text("dir1/dir2/dir3/dir4/dir5/hello.txt");
ok($hi0 eq $hi1, "nested directories");

kill_mount($pid);
unmount();
$pid = mount();

ok(-f "mnt/dir1/dir2/dir3/dir4/dir5/hello.txt", "file in nested directories exists after remount");

system("mkdir mnt/numbers");

for my $ii (1..300) {
    open(my $fh, '>', "mnt/numbers/$ii.num");
    close $fh;
}

kill_mount($pid);
unmount();
$pid = mount();

my $nn = `ls mnt/numbers | wc -l`;
ok($nn == 300, "created 300 files");

for my $ii (1..300) {
    write_text("numbers/$ii.num", "$ii");
}

for my $ii (1..30) {
    my $xx = $ii * 10;
    my $yy = read_text("numbers/$xx.num");
    ok($yy =~ /^\d+$/ && $xx == $yy, "check value $xx");
}

for my $ii (1..150) {
    my $xx = $ii * 2;
    system("rm mnt/numbers/$xx.num");
}

kill_mount($pid);
unmount();

ok(!-d "mnt/numbers", "numbers dir doesn't exist after umount");

mount();

my $mm = `ls mnt/numbers | wc -l`;
ok($mm == 150, "deleted 150 files");

unmount();

system("(make clean 2>&1) > /dev/null");
