FROM alpine:3.4
RUN apk update && apk upgrade && apk add gcc musl-dev
COPY listen.c .
RUN cc -Os listen.c -o listen && strip listen
RUN printf 'FROM scratch\nCOPY listen /\nENTRYPOINT ["/listen"]' > Dockerfile
CMD ["tar", "cf", "-", "Dockerfile", "listen"]
