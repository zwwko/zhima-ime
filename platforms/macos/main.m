#import <Cocoa/Cocoa.h>
#import <InputMethodKit/InputMethodKit.h>
#include "zhima.h"
/** 每个输入上下文持有独立 Rust 会话，负责组合文本及候选窗口。 */
@interface ZFController : IMKInputController
@property ZfHandle *engine;
@property NSPanel *panel;
@property NSTextField *label;
@end
@implementation ZFController
/** 初始化会话并加载随应用发布的词典，失败时保持按键直通。 */
- (instancetype)initWithServer:(IMKServer *)server delegate:(id)delegate client:(id)client {
 self=[super initWithServer:server delegate:delegate client:client];
 if(self){ NSString *r=[[NSBundle mainBundle] resourcePath];self.engine=zf_create([[r stringByAppendingPathComponent:@"wubi.tsv"] UTF8String],[[r stringByAppendingPathComponent:@"pinyin.tsv"] UTF8String]);
 self.panel=[[NSPanel alloc] initWithContentRect:NSMakeRect(0,0,500,64) styleMask:NSWindowStyleMaskBorderless|NSWindowStyleMaskNonactivatingPanel backing:NSBackingStoreBuffered defer:NO];self.panel.level=NSPopUpMenuWindowLevel;self.panel.hasShadow=YES;self.label=[NSTextField wrappingLabelWithString:@""];self.label.frame=NSMakeRect(10,8,480,48);self.label.font=[NSFont systemFontOfSize:18];[self.panel.contentView addSubview:self.label];}return self;
}
/** 将平台键盘事件规范化；带修饰键的快捷键交还应用。 */
- (BOOL)handleEvent:(NSEvent *)event client:(id)sender {
 if(!self.engine||event.type!=NSEventTypeKeyDown)return NO;
 if(event.modifierFlags&(NSEventModifierFlagCommand|NSEventModifierFlagControl|NSEventModifierFlagOption))return NO;
 NSString *key=event.characters;
 switch(event.keyCode){case 36:key=@"Enter";break;case 49:key=@"Space";break;case 51:key=@"Backspace";break;case 53:key=@"Escape";break;case 125:key=@"Down";break;case 126:key=@"Up";break;case 116:key=@"PageUp";break;case 121:key=@"PageDown";break;}
 if(!key.length)return NO;char *json=zf_process(self.engine,key.UTF8String);if(!json)return NO;
 NSData *data=[[NSString stringWithUTF8String:json] dataUsingEncoding:NSUTF8StringEncoding];zf_string_free(json);NSDictionary *out=[NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
 for(NSDictionary *commit in out[@"commits"]){@try{[sender insertText:commit[@"text"] replacementRange:NSMakeRange(NSNotFound,NSNotFound)];zf_ack_commit(self.engine,[commit[@"id"] unsignedLongLongValue],true);}@catch(NSException *exception){zf_ack_commit(self.engine,[commit[@"id"] unsignedLongLongValue],false);}}
 NSString *raw=out[@"preedit"]?:@"";[sender setMarkedText:raw selectionRange:NSMakeRange(raw.length,0) replacementRange:NSMakeRange(NSNotFound,NSNotFound)];
 NSMutableArray *items=[NSMutableArray array];NSUInteger i=1;for(NSDictionary *c in out[@"candidates"]){[items addObject:[NSString stringWithFormat:@"%lu.%@ %@",(unsigned long)i++,c[@"text"],c[@"wubi_hint"]]];}self.label.stringValue=[items componentsJoinedByString:@"  "];
 if(items.count){NSRect caret=NSZeroRect;[sender attributesForCharacterIndex:0 lineHeightRectangle:&caret];NSScreen *screen=self.panel.screen?:[NSScreen mainScreen];NSRect area=screen.visibleFrame;CGFloat x=MAX(NSMinX(area),MIN(caret.origin.x,NSMaxX(area)-500));CGFloat y=MAX(NSMinY(area),MIN(caret.origin.y-70,NSMaxY(area)-64));[self.panel setFrameOrigin:NSMakePoint(x,y)];[self.panel orderFrontRegardless];}else [self.panel orderOut:nil];return [out[@"consumed"] boolValue];
}
/** 订阅按键事件；Shift 单击尚未在本接入层实现。 */
- (NSUInteger)recognizedEvents:(id)sender {return NSEventMaskKeyDown;}
/** 焦点离开时取消组合，确保不向其他字段提交旧文本。 */
- (void)deactivateServer:(id)sender {if(self.engine){char *s=zf_process(self.engine,"FocusOut");zf_string_free(s);}[sender setMarkedText:@"" selectionRange:NSMakeRange(0,0) replacementRange:NSMakeRange(NSNotFound,NSNotFound)];[self.panel orderOut:nil];}
/** 释放独占会话资源。 */
- (void)dealloc {zf_destroy(self.engine);}
@end
/** 启动由 macOS 输入法系统管理的服务；安装由用户独立执行。 */
int main(int argc,const char *argv[]){@autoreleasepool{[NSApplication sharedApplication];IMKServer *server=[[IMKServer alloc] initWithName:@"ZhimaConnection" bundleIdentifier:@"org.zhima.inputmethod"];if(!server)return 1;[NSApp run];}return 0;}
