/* tsx sample for rampart-treesitter tests.
 * Covers: same surface as TypeScript, with JSX expressions in
 * function bodies to verify the tsx grammar handles them. */

function Hello({ name }: { name: string }) {
    return <div className="greeting">Hello {name}</div>;
}

function Counter() {
    return <button onClick={() => console.log("click")}>Click</button>;
}

class Modal {
    visible: boolean;
    constructor(visible: boolean) { this.visible = visible; }
    render() {
        return this.visible
            ? <div className="modal-open">open</div>
            : null;
    }
}

interface Props {
    title: string;
    onClose?: () => void;
}

type Renderable = JSX.Element | string;

enum Theme {
    LIGHT,
    DARK,
}

function topLevelLast<P>(props: P): P {
    return props;
}
