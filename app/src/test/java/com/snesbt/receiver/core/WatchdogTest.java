package com.snesbt.receiver.core;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public class WatchdogTest {

    @Test public void disarmedUntilFirstFeed() {
        Watchdog w = new Watchdog(100);
        assertFalse(w.poll(10_000));
        assertEquals(-1, w.remainingMs(0));
    }

    @Test public void expiresOnceAfterTimeout() {
        Watchdog w = new Watchdog(100);
        w.feed(1000);
        assertFalse(w.poll(1099));
        assertEquals(1, w.remainingMs(1099));
        assertTrue(w.poll(1100));
        assertFalse(w.poll(1200)); // não dispara repetidamente
        assertEquals(1, w.expirations());
    }

    @Test public void feedingKeepsItAlive() {
        Watchdog w = new Watchdog(100);
        for (long t = 0; t < 1000; t += 50) {
            w.feed(t);
            assertFalse(w.poll(t + 99));
        }
        assertEquals(0, w.expirations());
    }

    @Test public void rearmsAfterNewFeed() {
        Watchdog w = new Watchdog(50);
        w.feed(0);
        assertTrue(w.poll(60));
        w.feed(100);
        assertTrue(w.poll(150));
        assertEquals(2, w.expirations());
    }

    @Test public void disarmPreventsExpiry() {
        Watchdog w = new Watchdog(50);
        w.feed(0);
        w.disarm();
        assertFalse(w.poll(1000));
    }

    @Test(expected = IllegalArgumentException.class)
    public void rejectsNonPositiveTimeout() { new Watchdog(0); }
}
