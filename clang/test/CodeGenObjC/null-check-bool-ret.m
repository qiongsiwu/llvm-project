// RUN: %clang_cc1 -triple arm64e-apple-ios15.0.0-bc -fobjc-arc -disable-llvm-passes %s -emit-llvm -o - | FileCheck %s

@protocol NSObject
@end

@interface NSObject <NSObject>
@end

@interface WidgetTester : NSObject
@end

@implementation WidgetTester

typedef struct {
    NSObject* impl;
} widget_t;

- (_Bool)withWidget:(widget_t)widget {
    return 0;
}

- (_Bool)testWidget:(widget_t)widget {
    return [self withWidget:widget];
}

@end

// CHECK-LABEL: msgSend.call:
// CHECK: [[CALL:%[^ ]+]] = call i1 @objc_msgSend
// CHECK-NEXT: br label %msgSend.cont

// CHECK-LABEL: msgSend.null-receiver:
// CHECK: br label %msgSend.cont

// CHECK-LABEL: msgSend.cont:
// CHECK-NEXT: {{%[^ ]+}} = phi i1 [ [[CALL]], %msgSend.call ], [ false, %msgSend.null-receiver ]
