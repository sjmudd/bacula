################################################################
use strict;

=head1 LICENSE

   Bacula® - The Network Backup Solution

   Copyright (C) 2000-2009 Free Software Foundation Europe e.V.

   The main author of Bacula is Kern Sibbald, with contributions from
   many others, a complete list can be found in the file AUTHORS.

   This program is Free Software; you can redistribute it and/or
   modify it under the terms of version two of the GNU General Public
   License as published by the Free Software Foundation plus additions
   that are listed in the file LICENSE.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.

   Bacula® is a registered trademark of Kern Sibbald.
   The licensor of Bacula is the Free Software Foundation Europe
   (FSFE), Fiduciary Program, Sumatrastrasse 25, 8006 Zurich,
   Switzerland, email:ftf@fsfeurope.org.

=cut

package scripts::functions;
# Export all functions needed to be used by a simple 
# perl -Mscripts::functions -e '' script
use Exporter;
our @ISA = qw(Exporter);
our @EXPORT =  qw(update_some_files create_many_files check_multiple_copies
                  $cwd $bin $scripts $conf $rscripts $tmp $working
                  $db_name $db_user $db_password $src $tmpsrc);


our ($cwd, $bin, $scripts, $conf, $rscripts, $tmp, $working,
     $db_name, $db_user, $db_password, $src, $tmpsrc);

BEGIN {
    # start by loading the ./config file
    my ($envar, $enval);
    if (! -f "./config") {
        die "Could not find ./config file\n";
    }
    # load the ./config file in a subshell doesn't allow to use "env" to display all variable
    open(IN, ". ./config; env |") or die "Could not run shell: $!\n";
    while ( my $l = <IN> ) {
        chomp ($l);
        ($envar,$enval) = split (/=/,$l,2);
        $ENV{$envar} = $enval;
    }
    close(IN);
    $cwd = `pwd`; 
    chomp($cwd);

    # set internal variable name and update environment variable
    $ENV{db_name} = $db_name = $ENV{db_name} || 'regress';
    $ENV{db_user} = $db_user = $ENV{db_user} || 'regress';
    $ENV{db_password} = $db_password = $ENV{db_password} || '';

    $ENV{bin}      = $bin      =  $ENV{bin}      || "$cwd/bin";
    $ENV{tmp}      = $tmp      =  $ENV{tmp}      || "$cwd/tmp";
    $ENV{src}      = $src      =  $ENV{src}      || "$cwd/scr";
    $ENV{conf}     = $conf     =  $ENV{conf}     || $bin;
    $ENV{scripts}  = $scripts  =  $ENV{scripts}  || $bin;
    $ENV{tmpsrc}   = $tmpsrc   =  $ENV{tmpsrc}   || "$cwd/tmp/build";
    $ENV{working}  = $working  =  $ENV{working}  || "$cwd/working";    
    $ENV{rscripts} = $rscripts =  $ENV{rscripts} || "$cwd/scripts";
}

# open a directory and update all files
sub update_some_files
{
    my ($dest)=@_;
    my $t=rand();
    my $f;
    my $nb=0;
    print "Update files in $dest\n";
    opendir(DIR, $dest) || die "$!";
    map {
        $f = "$dest/$_";
        if (-f $f) {
            open(FP, ">$f") or die "$f $!";
            print FP "$t update $f\n";
            close(FP);
            $nb++;
        }
    } readdir(DIR);
    closedir DIR;
    print "$nb files updated\n";
}

# create big number of files in a given directory
# Inputs: dest  destination directory
#         nb    number of file to create
# Example:
# perl -Mscripts::functions -e 'create_many_files("$cwd/files", 100000)'
sub create_many_files
{
    my ($dest, $nb) = @_;
    my $base;
    my $dir=$dest;
    $nb = $nb || 750000;
    mkdir $dest;
    $base = chr($nb % 26 + 65); # We use a base directory A-Z

    # already done
    if (-f "$dest/$base/a${base}a${nb}aaa${base}") {
        print "Files already created\n";
        return;
    }

    # auto flush stdout for dots
    $| = 1;
    print "Create $nb files into $dest\n";
    for(my $i=0; $i < 26; $i++) {
        $base = chr($i + 65);
        mkdir("$dest/$base") if (! -d "$dest/$base");
    }
    for(my $i=0; $i<=$nb; $i++) {
        $base = chr($i % 26 + 65);
        open(FP, ">$dest/$base/a${base}a${i}aaa$base") or die "$dest/$base $!";
        print FP "$i\n";
        close(FP);
        
        open(FP, ">$dir/b${base}a${i}csq$base") or die "$dir $!";
        print FP "$base $i\n";
        close(FP);
        
        if (!($i % 100)) {
            $dir = "$dest/$base/$base$i$base";
            mkdir $dir;
        }
        print "." if (!($i % 10000));
    }
    print "\n";
}

sub check_encoding
{
    if (grep {/Wanted SQL_ASCII, got UTF8/} 
        `${bin}/bacula-dir -d50 -t -c ${conf}/bacula-dir.conf 2>&1`)
    {
        print "Found database encoding problem, please modify the ",
              "database encoding (SQL_ASCII)\n";
        exit 1;
    }
}

# This test ensure that 'list copies' displays only each copy one time
#
# Input: read stream from stdin or with file list argument
#        check the number of copies with the ARGV[1]
# Output: exit(1) if something goes wrong and print error
sub check_multiple_copies
{
    my ($nb_to_found) = @_;

    my $in_list_copies=0;       # are we or not in a list copies block
    my $nb_found=0;             # count the number of copies found
    my $ret = 0;
    my %seen;

    while (my $l = <>)          # read all files to check
    {
        if ($l =~ /list copies/) {
            $in_list_copies=1;
            %seen = ();
            next;
        }

        # not in a list copies anymore
        if ($in_list_copies && $l =~ /^ /) {
            $in_list_copies=0;
            next;
        }

        # list copies ouput:
        # |     3 | Backup.2009-09-28 |  9 | DiskChangerMedia |
        if ($in_list_copies && $l =~ /^\|\s+\d+/) {
            my (undef, $jobid, undef, $copyid, undef) = split(/\s*\|\s*/, $l);
            if (exists $seen{$jobid}) {
                print "ERROR: $jobid/$copyid already known as $seen{$jobid}\n";
                $ret = 1;
            } else {
                $seen{$jobid}=$copyid;
                $nb_found++;
            }
        }
    }
    
    # test the number of copies against the given arg
    if ($nb_to_found && ($nb_to_found != $nb_found)) {
        print "ERROR: Found wrong number of copies ",
              "($nb_to_found != $nb_found)\n";
        exit 1;
    }

    exit $ret;
}

1;
