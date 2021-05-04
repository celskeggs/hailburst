package timesync

import (
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"log"
	"net"
	"os"
	"os/signal"
	"sim/model"
)

type ProtocolImpl interface {
	Sync(pendingReadBytes int, now model.VirtualTime, writeData []byte) (expireAt model.VirtualTime, readData []byte)
}

func OnCancel(ctx context.Context, cb func()) {
	if ch := ctx.Done(); ch != nil {
		go func() {
			<-ch
			cb()
		}()
	}
}

func ListenUnix(ctx context.Context, sockpath string) (<-chan net.Conn, error) {
	if _, err := os.Lstat(sockpath); err == nil {
		log.Printf("Removing stale unix socket at %v", sockpath)
		if err := os.Remove(sockpath); err != nil {
			return nil, err
		}
	}
	l, err := net.Listen("unix", sockpath)
	if err != nil {
		return nil, err
	}
	halt := false
	OnCancel(ctx, func() {
		halt = true
		if err := l.Close(); err != nil {
			log.Printf("Unexpected error while closing unix socket: %v", err)
		}
		if err := os.Remove(sockpath); err != nil {
			log.Printf("Unexpected error while removing unix socket: %v", err)
		}
	})
	log.Printf("Listening to unix socket at %v", l.Addr())
	accepted := make(chan net.Conn)
	go func() {
		defer func() {
			if !halt {
				if err := l.Close(); err != nil {
					log.Printf("Unexpected error while closing unix socket: %v", err)
				}
			}
		}()
		defer close(accepted)
		for {
			conn, err := l.Accept()
			if err != nil {
				if !halt {
					log.Printf("Unexpected error while accepting connection from client: %v", err)
				}
				break
			}
			accepted <- conn
		}
	}()
	return accepted, nil
}

func ListenSingleConnection(ctx context.Context, sockpath string) (net.Conn, error) {
	conns, err := ListenUnix(ctx, sockpath)
	if err != nil {
		return nil, err
	}
	conn, ok := <-conns
	if !ok {
		return nil, errors.New("no clients attempted to connect")
	}
	go func() {
		for conn := range conns {
			log.Printf("Received unexpected connection on single-connection socket.")
			// close immediately
			err := conn.Close()
			if err != nil {
				log.Printf("Could not immediately close received connection: %v", err)
			}
		}
	}()
	return conn, nil
}

const (
	TimesyncLeaderMagic   = 0x71DE7EAD
	TimesyncFollowerMagic = 0x71DEF011
)

type TimesyncRequest struct {
	SeqNum           uint32
	PendingReadBytes uint32
	Now              int64
	Data             []byte
}

func convertU32ToU64(v uint32) uint64 {
	// always okay
	return uint64(v)
}

func convertU64ToU32(v uint64) uint32 {
	r := uint32(v)
	if v != uint64(r) {
		panic("integer out of range")
	}
	return r
}

func convertIntToU32(v int) uint32 {
	r := uint32(v)
	if v != int(r) {
		panic("integer out of range")
	}
	return r
}

func mergeU32sIntoU64(low, high uint32) uint64 {
	return convertU32ToU64(low) | (convertU32ToU64(high) << 32)
}

func splitU64IntoU32s(v uint64) (low, high uint32) {
	return convertU64ToU32(v & 0xFFFFFFFF), convertU64ToU32(v >> 32)
}

func mergeU32sIntoI64(low, high uint32) int64 {
	// allows negative numbers to be encoded via the most-significant bit
	return int64(mergeU32sIntoU64(low, high))
}

func splitI64IntoU32s(v int64) (low, high uint32) {
	// allows negative numbers to be encoded via the most-significant bit
	return splitU64IntoU32s(uint64(v))
}

func DecodeTimesyncRequest(r io.Reader) (TimesyncRequest, error) {
	var header struct {
		Magic            uint32
		SeqNum           uint32
		PendingReadBytes uint32
		NowLow           uint32
		NowHigh          uint32
		DataLength       uint32
	}
	if err := binary.Read(r, binary.BigEndian, &header); err != nil {
		return TimesyncRequest{}, err
	}
	if header.Magic != TimesyncLeaderMagic {
		return TimesyncRequest{}, fmt.Errorf(
			"received packet with magic number %x instead of %x", header.Magic, TimesyncLeaderMagic)
	}
	tsr := TimesyncRequest{
		SeqNum:           header.SeqNum,
		PendingReadBytes: header.PendingReadBytes,
		Now:              mergeU32sIntoI64(header.NowLow, header.NowHigh),
		Data:             make([]byte, header.DataLength),
	}
	if _, err := io.ReadFull(r, tsr.Data); err != nil {
		if err == io.EOF {
			err = io.ErrUnexpectedEOF
		}
		return TimesyncRequest{}, err
	}
	return tsr, nil
}

func DecodeTimesyncRequests(r io.Reader) <-chan TimesyncRequest {
	requests := make(chan TimesyncRequest)
	go func() {
		defer close(requests)
		for {
			request, err := DecodeTimesyncRequest(r)
			if err != nil {
				// if it's an EOF, that means we got disconnected at a valid packet boundary... that's fine!
				if err != io.EOF {
					log.Printf("Unexpected error while decoding packet from timesync connection: %v", err)
				}
				break
			}
			requests <- request
		}
	}()
	return requests
}

type TimesyncReply struct {
	SeqNum           uint32
	ExpireAt         int64
	Data             []byte
}

func EncodeTimesyncReply(w io.Writer, reply TimesyncReply) error {
	expireAtLow, expireAtHigh := splitI64IntoU32s(reply.ExpireAt)
	header := struct {
		Magic        uint32
		SeqNum       uint32
		ExpireAtLow  uint32
		ExpireAtHigh uint32
		DataLength   uint32
	}{
		Magic:        TimesyncFollowerMagic,
		SeqNum:       reply.SeqNum,
		ExpireAtLow:  expireAtLow,
		ExpireAtHigh: expireAtHigh,
		DataLength:   convertIntToU32(len(reply.Data)),
	}
	if err := binary.Write(w, binary.BigEndian, &header); err != nil {
		return err
	}
	if _, err := w.Write(reply.Data); err != nil {
		return err
	}
	return nil
}

func EncodeTimesyncReplies(w io.Writer) chan<- TimesyncReply {
	replies := make(chan TimesyncReply)
	go func() {
		for reply := range replies {
			err := EncodeTimesyncReply(w, reply)
			if err != nil {
				log.Printf("Unexpected error while encoding packet for timesync connection: %v", err)
			}
		}
	}()
	return replies
}

func HandleTimesyncProtocol(ctx context.Context, conn net.Conn, impl ProtocolImpl) error {
	OnCancel(ctx, func() {
		if err := conn.Close(); err != nil {
			log.Printf("Unexpected error while closing timesync connection: %v", err)
		}
	})
	requests := DecodeTimesyncRequests(conn)
	replies := EncodeTimesyncReplies(conn)
	defer close(replies)
	var nextSeqNum uint32 = 0
	for request := range requests {
		if request.SeqNum != nextSeqNum {
			return fmt.Errorf("unexpected sequence number %v instead of %v", request.SeqNum, nextSeqNum)
		}

		expireAt, data := impl.Sync(int(request.PendingReadBytes), model.VirtualTime(request.Now), request.Data)
		if request.PendingReadBytes > 0 && len(data) > 0 {
			return fmt.Errorf(
				"impermissible attempt to write %d bytes of data when %d were already pending",
				len(data), request.PendingReadBytes)
		}
		replies <- TimesyncReply{
			SeqNum:   request.SeqNum,
			ExpireAt: int64(expireAt),
			Data:     data,
		}

		nextSeqNum += 1
	}
	return nil
}

func OnInterrupt(ctx context.Context, cb func()) {
	ch := make(chan os.Signal)
	signal.Notify(ch, os.Interrupt)
	go func() {
		select {
		case <-ch:
			cb()
		case <-ctx.Done():
			// do nothing
		}
	}()
}

func Simple(sockpath string, app ProtocolImpl) error {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	OnInterrupt(ctx, cancel)
	conn, err := ListenSingleConnection(ctx, sockpath)
	if err != nil {
		return err
	}
	log.Printf("Leader connected to timesync protocol from %v", conn.RemoteAddr())
	if err := HandleTimesyncProtocol(ctx, conn, app); err != nil {
		return err
	}
	log.Printf("Exiting normally...")
	return nil
}
