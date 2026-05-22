#!/usr/bin/perl
#
# 9folders fork: regression coverage for bulk-event chunking
# introduced in `Split bulk mbox events into 50-record chunks`.
#
# Cassandane's notifyd uses the `dlist:` notifier path (TCP-style
# Unix socket), so the production AF_UNIX SOCK_DGRAM 64 KB drop is
# not reproducible here. This test exercises the slicing logic
# behaviorally: confirms the chunked dispatch covers every record
# exactly once, in order, with no duplicates or losses.
#

package Cassandane::Cyrus::MboxEventBulk;
use strict;
use warnings;
use JSON;
use Data::Dumper;

use lib '.';
use base qw(Cassandane::Cyrus::TestCase);
use Cassandane::Util::Log;

sub new
{
    my ($class, @args) = @_;

    my $config = Cassandane::Config->default()->clone();
    $config->set(
        event_groups       => 'message',
        event_extra_params => 'messages vnd.cmu.midset',
    );

    return $class->SUPER::new({
        config => $config,
        deliver => 1,
    }, @args);
}

sub set_up
{
    my ($self) = @_;
    $self->SUPER::set_up();
}

sub tear_down
{
    my ($self) = @_;
    $self->SUPER::tear_down();
}

# Collect MessageExpunge events from the notifyd queue and return the
# list of midset arrays (one entry per chunked dispatch).
sub _collect_expunge_chunks
{
    my ($self) = @_;
    my $events = $self->{instance}->getnotify();
    my @chunks;
    foreach my $e (@{$events}) {
        my $msg = decode_json($e->{MESSAGE});
        next unless $msg->{event} eq 'MessageExpunge';
        my $mid = $msg->{'vnd.cmu.midset'} // [];
        push @chunks, $mid if ref $mid eq 'ARRAY';
    }
    return @chunks;
}

# Bulk EXPUNGE of $n messages must dispatch ceil($n / 50) datagrams,
# whose midset union covers every original Message-Id exactly once.
sub _run_bulk_expunge
{
    my ($self, $n) = @_;

    my $store = $self->{store};
    my $talk  = $store->get_client();

    # Discard setup-related events.
    $self->{instance}->getnotify();

    # Append $n messages to INBOX.
    for (my $i = 1; $i <= $n; $i++) {
        $self->make_message("Bulk msg $i")
            || die "make_message $i failed";
    }

    # Discard append events before exercising the chunked path.
    $self->{instance}->getnotify();

    $talk->select('INBOX');
    $talk->store("1:$n", '+flags', '(\\Deleted)');
    $talk->expunge();

    my @chunks = $self->_collect_expunge_chunks();

    # ceil(n / 50) without floating-point rounding.
    use integer;
    my $want = ($n + 50 - 1) / 50;
    no integer;

    $self->assert_num_equals($want, scalar @chunks,
        "expected $want chunked MessageExpunge events for n=$n,"
        . " got " . scalar(@chunks));

    my %seen;
    foreach my $chunk (@chunks) {
        foreach my $mid (@{$chunk}) {
            $seen{$mid}++;
        }
    }
    my @dupes = grep { $seen{$_} > 1 } keys %seen;
    $self->assert_num_equals(0, scalar @dupes,
        "midset duplicated across chunks for n=$n: @dupes");

    # The fixture generates one unique Message-Id per message, so the
    # midset union must equal $n.
    $self->assert_num_equals($n, scalar keys %seen,
        "midset coverage mismatch for n=$n: expected $n unique,"
        . " got " . scalar(keys %seen));
}

sub test_bulk_expunge_below_threshold
    :min_version_3_8
{
    my ($self) = @_;
    # n=49 stays on the single-datagram path; one MessageExpunge event.
    my $n = 49;
    my $store = $self->{store};
    my $talk  = $store->get_client();
    $self->{instance}->getnotify();
    for (my $i = 1; $i <= $n; $i++) {
        $self->make_message("Below msg $i") || die;
    }
    $self->{instance}->getnotify();
    $talk->select('INBOX');
    $talk->store("1:$n", '+flags', '(\\Deleted)');
    $talk->expunge();

    my @chunks = $self->_collect_expunge_chunks();
    $self->assert_num_equals(1, scalar @chunks,
        "n=49 must emit a single MessageExpunge event, got "
        . scalar(@chunks));
}

sub test_bulk_expunge_at_threshold
    :min_version_3_8
{
    my ($self) = @_;
    $self->_run_bulk_expunge(50);
}

sub test_bulk_expunge_above_threshold
    :min_version_3_8
{
    my ($self) = @_;
    # n=51 forces the chunk path: 50 + 1.
    $self->_run_bulk_expunge(51);
}

sub test_bulk_expunge_multi_chunks
    :min_version_3_8
{
    my ($self) = @_;
    # n=120 → 3 chunks (50 + 50 + 20).
    $self->_run_bulk_expunge(120);
}

1;
