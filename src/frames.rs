pub struct EncodedFrame {
    pub data: Vec<u8>,
}

struct Node {
    frame: EncodedFrame,
    next: Option<Box<Node>>,
}

pub struct FrameList {
    head: Option<Box<Node>>,
    len: usize,
}

impl FrameList {
    pub fn new() -> Self { Self { head: None, len: 0 } }

    pub fn push(&mut self, frame: EncodedFrame) {
        self.head = Some(Box::new(Node { frame, next: self.head.take() }));
        self.len += 1;
    }

    pub fn len(&self) -> usize { self.len }

    pub fn pop(&mut self) -> Option<EncodedFrame> {
        self.head.take().map(|node| {
            self.head = node.next;
            self.len -= 1;
            node.frame
        })
    }
}

impl Drop for FrameList {
    fn drop(&mut self) {
        let mut node = self.head.take();
        while let Some(mut current) = node {
            node = current.next.take();
        }
    }
}