package readlog

import (
	"bufio"
	"fmt"
	"io"
	"path"
	"strings"
)

func Render(record Record) string {
	return record.Metadata.Format.Render(record.ArgumentData)
}

func Renderer(input <-chan Record, output io.Writer, full bool) error {
	bw := bufio.NewWriter(output)
	for line := range input {
		var err error
		if full {
			_, err = fmt.Fprintf(
				bw, "%08X - %-25s - %15v - %s\n",
				line.Metadata.MessageUID,
				fmt.Sprintf("%s:%d", line.Metadata.Filename, line.Metadata.LineNum),
				line.Timestamp, Render(line))
		} else {
			filename := path.Base(line.Metadata.Filename)
			if strings.HasSuffix(filename, ".c") {
				filename = filename[:len(filename)-2]
			}
			filename = strings.ToUpper(filename)
			_, err = fmt.Fprintf(
				bw, "[%13v] [%12s] %s\n", line.Timestamp, filename, Render(line))
		}
		if err != nil {
			return err
		}
		err = bw.Flush()
		if err != nil {
			return err
		}
	}
	return nil
}
